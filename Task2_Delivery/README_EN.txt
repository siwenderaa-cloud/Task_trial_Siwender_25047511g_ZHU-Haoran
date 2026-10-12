TASK 2 CAMERA FEASIBILITY AND ARDUINO IMPLEMENTATION
Date: 12 October 2026

RESULT
HBVCAM F2170HD V66 captured real 640 x 480 MJPEG frames on ESP32-P4 at
29.1 to 30.0 fps in the observed short test. One complete 16712-byte JPEG
was saved and opened. Camera capture passed. Overall startup stability
has not passed: a TTL reset caused a bootloader fault followed by automatic
watchdog recovery before the application ran.

READ IN THIS ORDER
1. Task2_Camera_Feasibility_and_Implementation_EN.docx
   Combined Part A and Part B report, including all three cameras,
   implementation, measured results, compile/flash note and delivery mapping.
2. COMPILE_FLASH_EN.txt
   IDE settings, exact CLI configuration, expected serial output and snapshot use.
3. HBVCAM_USB_PartB/HBVCAM_USB_PartB.ino
   Working full sketch. Keep src/UsbCamera.h and src/UsbCamera.cpp beside it.
   No extra Arduino library install is needed with tested Espressif core 3.3.12.
4. VERIFICATION_EN.txt and HBVCAM_USB_PartB/evidence/
   Actual build, upload, startup, stream and snapshot records.
5. AI_CONVERSATION_LINKS.txt
   Five public chat share snapshots generated with explicit permission.
   Secondary local Codex references are also retained for traceability.

ACTUAL CAMERA EVIDENCE
HBVCAM_USB_PartB/hbvcam_640x480.jpg is the unmodified live camera frame.
The image is upside down because of physical camera orientation.
No sample photo is embedded in the Arduino firmware.
evidence/hardware_summary.json distinguishes camera_capture_verified=true
from startup_stability_verified=false. The mode CSV lists advertised modes;
it is not evidence that all listed resolutions were captured.

SCOPE AND LIMITATIONS
OV2710 overheated during bring-up and its exact pinout is unresolved; no
OV2710 ID or captured image is claimed. IMX219 was researched but not tested.
The final submission code targets ESP32-P4, not the original ESP32-WROOM.
1080p capture, YUY2 capture/decode, cold power-cycle startup, camera hotplug,
long-duration load, network streaming and display output were not verified.
The macOS/POSIX Python snapshot helper was not validated on Windows.
Word pages were rendered and visually checked; native Windows Word was not tested.

LICENSING AND FILE INTEGRITY
Espressif UVC driver/example reuse is attributed in the report and sketch README.
The Apache 2.0 license is included in HBVCAM_USB_PartB/LICENSE.
SHA256SUMS.json at the package root covers every submitted file except itself.
The nested sketch SHA256SUMS.json describes the original verified camera package.
The original Part A report and existing camera packages were preserved.
