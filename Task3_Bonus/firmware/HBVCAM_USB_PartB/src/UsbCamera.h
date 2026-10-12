#pragma once
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <usb/uvc_host.h>

#if !CONFIG_IDF_TARGET_ESP32P4
#error "This sketch targets ESP32-P4 with Arduino core 3.3.12."
#endif

// One instance, called from setup/loop. Uses native HS USB, not SCCB GPIOs.
// HBVCAM -> OSPTEK USB-A; TTL -> Mac; middle USB-C left disconnected.
struct UsbCameraConfig {
  unsigned width = 640;
  unsigned height = 480;
};

class UsbCamera {
 public:
  bool begin(const UsbCameraConfig &config = UsbCameraConfig());
  bool startStream(uint32_t waitMs = 500);
  bool streaming() const { return running_ && !disconnected_.load(); }
  // A borrowed frame: call releaseFrame exactly once before the next capture.
  const uvc_host_frame_t *captureFrame(uint32_t waitMs = 1500);
  void releaseFrame(const uvc_host_frame_t *frame);
  // Copies one MJPEG frame, returns all borrowed buffers, pauses USB streaming,
  // transfers JPEG over UART, then resumes. No software JPEG encoder required.
  bool sendSnapshot(Stream &serial);
  const char *lastError() const { return error_; }
  uint32_t usbErrors() const { return errors_.load(); }
  uint32_t droppedFrames() const { return dropped_.load(); }
  uint32_t overflows() const { return overflows_.load(); }

 private:
  struct Connection { uint8_t address, index; };
  static void hostTask(void *arg);
  static void driverEvent(const uvc_host_driver_event_data_t *event, void *ctx);
  static void streamEvent(const uvc_host_stream_event_data_t *event, void *ctx);
  static bool frameEvent(const uvc_host_frame_t *frame, void *ctx);
  bool fail(const char *message) { error_ = message; return false; }
  void drainFrames();
  QueueHandle_t connections_ = nullptr, frames_ = nullptr;
  uvc_host_stream_hdl_t stream_ = nullptr;
  std::atomic<bool> disconnected_{false};
  std::atomic<uint32_t> errors_{0}, dropped_{0}, overflows_{0};
  bool initialized_ = false, running_ = false;
  UsbCameraConfig config_;
  const char *error_ = "OK";
};
