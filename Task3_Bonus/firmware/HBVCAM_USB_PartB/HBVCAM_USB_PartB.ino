/* HBVCAM F2170HD V66 USB/UVC trial, Arduino style.
   ESP32P4 Dev Module, Espressif core 3.3.12, Chip Variant v3.00 or newer,
   PSRAM Enabled, Flash16MB DIO/40MHz, Huge APP, USB CDC On Boot Disabled.
   HBVCAM -> board USB-A; TTL -> Mac; download selector -> P4.
   Leave middle USB-C empty: this OSPTEK board shares its USB data path.
   Disconnect the previous OV2710 MIPI cable; this sketch never uses it.
   Native USB-HS pins/PHY, no arbitrary GPIO pin mapping and no USBHost.begin().
   Reuses the bundled Espressif usb_host_uvc host driver. See README.
*/
#include <Arduino.h>
#include "src/UsbCamera.h"

UsbCamera camera;
bool initialized = false;
uint32_t frames = 0, windowFrames = 0, lastReport = 0, lastWait = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.printf("\n=== HBVCAM USB / ESP32-P4 / Arduino ===\nChip=%s rev=%u PSRAM=%u\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getPsramSize());
  UsbCameraConfig config; // Prefer advertised MJPEG 640x480; otherwise smallest MJPEG.
  initialized = camera.begin(config);
  if (!initialized) Serial.printf("[FAIL] %s\n", camera.lastError());
  else Serial.println("[WAIT] Connect UVC camera to USB-A. Command j=JPEG snapshot via Mac helper.");
}

void loop() {
  if (!initialized) { delay(1000); return; }
  if (!camera.streaming()) {
    if (!camera.startStream()) {
      if (millis() - lastWait >= 5000) {
        Serial.printf("[WAIT] %s\n", camera.lastError());
        lastWait = millis();
      }
      return;
    }
    windowFrames = 0;
    lastReport = millis();
  }
  while (Serial.available()) {
    if (Serial.read() == 'j') {
      if (!camera.sendSnapshot(Serial)) Serial.printf("[JPEG FAIL] %s\n", camera.lastError());
      windowFrames = 0;
      lastReport = millis();
    }
  }
  const uvc_host_frame_t *frame = camera.captureFrame();
  if (!frame) {
    Serial.printf("[CAPTURE FAIL] %s\n", camera.lastError());
    delay(100);
    return;
  }
  ++frames;
  ++windowFrames;
  const uint32_t now = millis();
  if (now - lastReport >= 1000) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < frame->data_len; i += 97) hash = (hash ^ frame->data[i]) * 16777619u;
    Serial.printf("[FRAME] total=%lu fps=%.1f size=%u width=%u height=%u hash=%08lX usbErrors=%lu drops=%lu overflows=%lu freePSRAM=%u\n",
                  (unsigned long)frames, windowFrames * 1000.0f / (now - lastReport),
                  unsigned(frame->data_len), frame->vs_format.h_res, frame->vs_format.v_res,
                  (unsigned long)hash, (unsigned long)camera.usbErrors(),
                  (unsigned long)camera.droppedFrames(), (unsigned long)camera.overflows(), ESP.getFreePsram());
    windowFrames = 0;
    lastReport = now;
  }
  camera.releaseFrame(frame);
}
