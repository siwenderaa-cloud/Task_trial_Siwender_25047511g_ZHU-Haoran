// SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// Uses Espressif usb_host_uvc bundled with Arduino core 3.3.12.
// Adapted from ESP-IDF v5.5.5 peripherals/usb/host/uvc (Apache-2.0).
#include "UsbCamera.h"
#include <esp_heap_caps.h>
#include <esp_intr_alloc.h>
#include <esp_log.h>
#include <freertos/task.h>
#include <usb/usb_host.h>

static std::atomic<uint32_t> enumeratedId{0};
static bool enumFilter(const usb_device_desc_t *desc, uint8_t *configuration) {
  // Mandatory with this core's CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK.
  // No transfers or printing from the enumeration callback.
  enumeratedId.store((uint32_t(desc->idVendor) << 16) | desc->idProduct);
  *configuration = 1;
  return true;
}

void UsbCamera::hostTask(void *) {
  while (true) {
    uint32_t flags = 0;
    if (usb_host_lib_handle_events(portMAX_DELAY, &flags) == ESP_OK &&
        (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)) usb_host_device_free_all();
  }
}

void UsbCamera::driverEvent(const uvc_host_driver_event_data_t *event, void *ctx) {
  auto *self = static_cast<UsbCamera *>(ctx);
  if (event->type == UVC_HOST_DRIVER_EVENT_DEVICE_CONNECTED) {
    const Connection connection{event->device_connected.dev_addr,
                                event->device_connected.uvc_stream_index};
    if (connection.index == 0) xQueueOverwrite(self->connections_, &connection);
  }
}

void UsbCamera::streamEvent(const uvc_host_stream_event_data_t *event, void *ctx) {
  auto *self = static_cast<UsbCamera *>(ctx);
  switch (event->type) {
    case UVC_HOST_DEVICE_DISCONNECTED: self->disconnected_.store(true); break;
    case UVC_HOST_TRANSFER_ERROR: ++self->errors_; break;
    case UVC_HOST_FRAME_BUFFER_OVERFLOW: ++self->overflows_; break;
    case UVC_HOST_FRAME_BUFFER_UNDERFLOW: ++self->dropped_; break;
    default: break;
  }
}

bool UsbCamera::frameEvent(const uvc_host_frame_t *frame, void *ctx) {
  auto *self = static_cast<UsbCamera *>(ctx);
  if (xQueueSend(self->frames_, &frame, 0) == pdPASS) return false;
  ++self->dropped_;
  return true; // Return ownership immediately if the application queue is full.
}

bool UsbCamera::begin(const UsbCameraConfig &config) {
  if (initialized_) return fail("begin() already called");
  if (!psramFound()) return fail("Enable PSRAM in Arduino Tools");
  config_ = config;
  connections_ = xQueueCreate(1, sizeof(Connection));
  frames_ = xQueueCreate(2, sizeof(uvc_host_frame_t *));
  if (!connections_ || !frames_) return fail("Cannot allocate queues");
  usb_host_config_t host = {};
  host.skip_phy_setup = false;
  host.intr_flags = ESP_INTR_FLAG_LEVEL1;
  host.enum_filter_cb = enumFilter;
  host.peripheral_map = 0; // P4 default = native High-Speed controller.
  esp_err_t err = usb_host_install(&host);
  Serial.printf("[USB_HOST] install=%s native=HS\n", esp_err_to_name(err));
  if (err != ESP_OK) return fail("USB host install failed");
  if (xTaskCreate(hostTask, "usb_lib", 4096, nullptr, 15, nullptr) != pdPASS)
    return fail("USB event task allocation failed; restart before retry");
  uvc_host_driver_config_t uvc = {};
  uvc.driver_task_stack_size = 4096;
  uvc.driver_task_priority = 16;
  uvc.xCoreID = tskNO_AFFINITY;
  uvc.create_background_task = true;
  uvc.event_cb = driverEvent;
  uvc.user_ctx = this;
  err = uvc_host_install(&uvc);
  Serial.printf("[UVC_DRIVER] install=%s\n", esp_err_to_name(err));
  if (err != ESP_OK) return fail("UVC driver install failed; restart before retry");
  initialized_ = true;
  return true;
}

void UsbCamera::drainFrames() {
  const uvc_host_frame_t *frame = nullptr;
  while (xQueueReceive(frames_, &frame, 0) == pdPASS) releaseFrame(frame);
}

bool UsbCamera::startStream(uint32_t waitMs) {
  if (!initialized_) return fail("Call begin() first");
  if (stream_ && disconnected_.load()) {
    running_ = false;
    drainFrames();
    if (uvc_host_stream_close(stream_) != ESP_OK) return fail("Disconnected stream close failed");
    stream_ = nullptr;
    disconnected_.store(false);
    Serial.println("[UVC_DISCONNECTED]");
  }
  if (!stream_) {
    Connection connection = {};
    if (xQueueReceive(connections_, &connection, pdMS_TO_TICKS(waitMs)) != pdPASS)
      return fail("Waiting for UVC camera on USB-A");
    const uint32_t id = enumeratedId.load();
    Serial.printf("[UVC_CONNECTED] address=%u VID=0x%04X PID=0x%04X\n",
                  connection.address, unsigned(id >> 16), unsigned(id & 0xFFFF));
    size_t count = 0;
    esp_err_t err = uvc_host_get_frame_list(connection.address, connection.index, nullptr, &count);
    if (err != ESP_OK || !count || count > 256) return fail("Cannot read UVC frame descriptors");
    auto *modes = static_cast<uvc_host_frame_info_t *>(calloc(count, sizeof(uvc_host_frame_info_t)));
    if (!modes) return fail("Mode list allocation failed");
    err = uvc_host_get_frame_list(connection.address, connection.index,
                                reinterpret_cast<uvc_host_frame_info_t (*)[]>(modes), &count);
    int selected = -1;
    if (err == ESP_OK) {
      for (size_t i = 0; i < count; ++i) {
        Serial.printf("[UVC_MODE] index=%u format=%u width=%u height=%u defaultInterval=%lu\n",
                      unsigned(i), unsigned(modes[i].format), modes[i].h_res, modes[i].v_res,
                      (unsigned long)modes[i].default_interval);
        if (modes[i].format != UVC_VS_FORMAT_MJPEG) continue;
        if (selected < 0 || uint64_t(modes[i].h_res) * modes[i].v_res <
                            uint64_t(modes[selected].h_res) * modes[selected].v_res) selected = i;
      }
    }
    if (selected < 0) { free(modes); return fail("Camera advertises no MJPEG mode; no stream started"); }
    // Prefer the requested dimensions only if the real camera advertises them.
    // Otherwise retain the smallest MJPEG mode as a conservative fallback.
    for (size_t i = 0; i < count; ++i) {
      if (modes[i].format == UVC_VS_FORMAT_MJPEG && modes[i].h_res == config_.width &&
          modes[i].v_res == config_.height) { selected = i; break; }
    }
    uvc_host_stream_config_t cfg = {};
    cfg.event_cb = streamEvent;
    cfg.frame_cb = frameEvent;
    cfg.user_ctx = this;
    cfg.usb.dev_addr = connection.address;
    cfg.usb.uvc_stream_index = connection.index;
    cfg.vs_format.h_res = modes[selected].h_res;
    cfg.vs_format.v_res = modes[selected].v_res;
    cfg.vs_format.format = UVC_VS_FORMAT_MJPEG;
    cfg.vs_format.fps = 0; // Use this advertised mode's default frame interval.
    cfg.advanced.number_of_frame_buffers = 3;
    cfg.advanced.frame_size = 0; // Use the device's negotiated maximum frame size.
    cfg.advanced.frame_heap_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    cfg.advanced.number_of_urbs = 3;
    cfg.advanced.urb_size = 10 * 1024;
    free(modes);
    err = uvc_host_stream_open(&cfg, pdMS_TO_TICKS(3000), &stream_);
    Serial.printf("[UVC_OPEN] %s\n", esp_err_to_name(err));
    if (err != ESP_OK) { stream_ = nullptr; return fail("UVC open/format negotiation failed; reconnect camera to retry"); }
    // Prints real USB descriptors, including speed, VID/PID and frame modes.
    uvc_host_desc_print(stream_);
  }
  if (!running_) {
    const esp_err_t err = uvc_host_stream_start(stream_);
    if (err != ESP_OK) return fail("UVC stream start failed");
    running_ = true;
    uvc_host_stream_format_t format = {};
    uvc_host_stream_format_get(stream_, &format);
    Serial.printf("[READY] MJPEG %ux%u nominalFPS=%.2f\n", format.h_res, format.v_res, format.fps);
  }
  error_ = "OK";
  return true;
}

const uvc_host_frame_t *UsbCamera::captureFrame(uint32_t waitMs) {
  if (!streaming()) { fail("Camera disconnected or stream stopped"); return nullptr; }
  const uvc_host_frame_t *frame = nullptr;
  if (xQueueReceive(frames_, &frame, pdMS_TO_TICKS(waitMs)) != pdPASS) {
    fail("No frame received before timeout");
    return nullptr;
  }
  if (!frame->data || frame->data_len < 4 || frame->data_len > frame->data_buffer_len ||
      frame->vs_format.format != UVC_VS_FORMAT_MJPEG ||
      frame->data[0] != 0xFF || frame->data[1] != 0xD8 ||
      frame->data[frame->data_len - 2] != 0xFF || frame->data[frame->data_len - 1] != 0xD9) {
    releaseFrame(frame);
    fail("Frame is not a complete MJPEG image");
    return nullptr;
  }
  error_ = "OK";
  return frame;
}

void UsbCamera::releaseFrame(const uvc_host_frame_t *frame) {
  if (frame && stream_) uvc_host_frame_return(stream_, const_cast<uvc_host_frame_t *>(frame));
}

bool UsbCamera::sendSnapshot(Stream &serial) {
  const uvc_host_frame_t *frame = captureFrame();
  if (!frame) return false;
  const size_t size = frame->data_len;
  const unsigned width = frame->vs_format.h_res, height = frame->vs_format.v_res;
  auto *copy = size <= 1024 * 1024 ? static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) : nullptr;
  if (copy) memcpy(copy, frame->data, size);
  releaseFrame(frame);
  if (!copy) return fail("Snapshot exceeds 1MiB or allocation failed");
  const esp_err_t stopped = uvc_host_stream_stop(stream_);
  if (stopped != ESP_OK) { free(copy); return fail("Cannot pause camera for UART snapshot"); }
  running_ = false;
  drainFrames();
  const esp_log_level_t previous = esp_log_level_get("*");
  esp_log_level_set("*", ESP_LOG_NONE);
  serial.printf("\nJPEG %u %u %u\n", unsigned(size), width, height);
  size_t sent = 0;
  while (sent < size) {
    const size_t n = serial.write(copy + sent, size - sent);
    if (!n) break;
    sent += n;
    yield();
  }
  serial.print("\nEND_JPEG\n");
  serial.flush();
  esp_log_level_set("*", previous);
  free(copy);
  const bool resumed = startStream(0);
  return (sent == size && resumed) || fail("Snapshot transfer or stream resume failed");
}
