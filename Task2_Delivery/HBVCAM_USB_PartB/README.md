# HBVCAM USB 摄像头 / ESP32-P4 / Arduino Part B

本程序针对用户的 HBVCAM F2170HD V66，以 USB UVC 摄像头方式接入 ESP32-P4。USB Host、UVC 枚举/格式协商和视频传输复用 Arduino core 3.3.12 内置的 Espressif 驱动；不依赖 OV2710、MIPI 排线、屏幕或 C5 Wi-Fi。真实执行结果见 VERIFICATION.md；编译通过本身不代表摄像头已出图。

## 接线

- 用户已确认原 OV2710 排线拔掉，HBVCAM 接开发板 USB-A，右边 TTL USB-C 接 Mac，中间 USB-C 未接线；下载开关选择 P4。
- USB 摄像头使用 USB-A 提供的 5V 和 D+/D-，经底板接至 P4 原生高速 USB PHY。程序不配置普通 GPIO、SDA/SCL 或摄像头寄存器。
- 此 OSPTEK 参考底板的 USB-A 与中间 USB-C 共用数据路径，公开原理图显示中间口供电存在时会切换数据通道，因此本次 USB-A 测试保持中间口空置。若供电不足，应先核对实际底板版本及 USB Host 供电方式。
- 本程序使用 ESP-IDF USB Host 栈；不要同时调用 TinyUSB `USBHost.begin()` 或启用原生 USB CDC 占用同一控制器。

## Arduino IDE

打开 `HBVCAM_USB_PartB.ino`，同目录的 `src/UsbCamera.h/.cpp` 自动编译，无需手工安装额外库。

| 设置 | 值 |
|---|---|
| 开发板包 | esp32 by Espressif Systems 3.3.12 |
| Board | ESP32P4 Dev Module |
| Chip Variant | v3.00 or newer（实测芯片 v3.2） |
| PSRAM | Enabled |
| Flash | 16MB、DIO、40MHz |
| Partition | Huge APP |
| USB CDC On Boot | Disabled（通过 TTL UART 通信） |
| Upload Speed | 460800 |
| Serial Monitor | 115200 |

本机 CLI 编译额外使用 `--build-property build.img_freq=40m`，使镜像头与 40MHz 启动器一致。未修改系统开发板包；普通 IDE 下选 Flash Frequency=40MHz 未必会同步修改这一镜像属性。已验证过的具体构建配置和结果保留在项目日志中。

## 程序结构及 Part B 对应

- `.ino`：`setup()` 调用 `begin()`，`loop()` 启动视频流、获取并释放帧、打印帧率/内容摘要，响应快照命令。
- `src/UsbCamera.h/.cpp`：USB 接口说明、`begin()`、`startStream()`、`captureFrame()`、`releaseFrame()`、`sendSnapshot()`。
- 枚举回调选择配置 1（此 core 必须提供）；UVC 连接后读取摄像头真实描述符，优先选择 `UsbCameraConfig` 指定的 MJPEG 尺寸（默认 640×480，已在本机描述符中读到），不存在时退回最小 MJPEG 模式，使用模式公布的默认帧率。没有套用 V22 或其他型号的规格。
- 三个 PSRAM 帧缓冲，后台回调把借用帧放入队列；队列满时立即归还。调用者必须在处理完后释放帧。断连时先清空并归还队列帧，再关闭流。
- 快照复制一张完整 JPEG 后归还帧，暂停 USB 视频流，再经 TTL UART 发送；传输后恢复视频流，避免 UART 慢速传输占用全部摄像头缓冲。
- `capture_hbvcam.py`：Mac/POSIX 标准库脚本，读取日志或保存真实 JPEG，校验字节数、JPEG 起止标记和协议尾部。

预期日志示例（不是本机实测值）：

```text
[UVC_CONNECTED] address=... VID=0x.... PID=0x....
[UVC_MODE] index=... format=1 width=... height=...
[READY] MJPEG ...x... nominalFPS=...
[FRAME] total=... fps=... size=... hash=... usbErrors=... drops=...
```

若摄像头没有公布 MJPEG 模式，本版会明确报错，不假装已经采帧。本版没有实现 YUY2/H.264 解码或网络直播。它验证 USB 摄像头采帧和 JPEG 快照；这是本次试做的范围。

## 保存图片和日志

关闭 Arduino Serial Monitor，然后运行：

```sh
python3 capture_hbvcam.py --port /dev/cu.usbmodem5B790679131 --output hbvcam_snapshot.jpg
python3 capture_hbvcam.py --port /dev/cu.usbmodem5B790679131 --monitor 20
```

程序会发送 `j` 命令，接收一张 JPEG。照片来自摄像头，不是在编译时嵌入的图片。UART 为 115200 baud，传输会暂停实时采帧；输出路径可改。串口设备名以当前 Mac 枚举结果为准。

## 来源与适配

- [Espressif Arduino core 3.3.12](https://github.com/espressif/arduino-esp32/releases/tag/3.3.12)，本机包含 `usb_host_uvc` 头文件和预编译库。
- [ESP-IDF v5.5.5 UVC Host 示例（Apache-2.0）](https://github.com/espressif/esp-idf/blob/v5.5.5/examples/peripherals/usb/host/uvc/main/main.c)：USB 事件任务、UVC 驱动初始化、回调/队列和帧归还方式。
- [Espressif USB Host UVC 驱动（Apache-2.0）](https://github.com/espressif/esp-usb/tree/master/host/class/uvc/usb_host_uvc)：驱动实现仍使用开发板包自带版本，未下载或替换库。
- [Arduino USB Host 枚举回调要求](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/usb.html)：core 3.3.11 起需提供 `enum_filter_cb`。
- [OSPTEK 底板资料与原理图](https://github.com/osptek/esp32-p4c5-module-dev-board)：USB-A、TTL、USB 数据切换及供电结构。

新增适配：Arduino setup/loop 包装、真实模式枚举与选择、明确的帧借用 API、JPEG 完整性检查、断连处理、帧计数和暂停/恢复式 UART 快照。Mac helper 复用本项目 OV2710 快照协议，改为接收 USB 摄像头已经编码的 MJPEG 帧。

原版 ESP32-WROOM 没有原生 USB Host，不能直接使用这个接法和程序；本次对象是有原生 USB Host 的 ESP32-P4。HBVCAM V66 的完整实际能力以本机读到的 USB 描述符和实测为准。
