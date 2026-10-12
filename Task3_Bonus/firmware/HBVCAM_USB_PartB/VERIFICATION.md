# HBVCAM USB/UVC 实机验证 — 2026-10-12

**USB 摄像头已经实际出图：640×480 MJPEG，约 30fps，已保存并打开检查真实 JPEG。开发板的 TTL 硬件复位启动异常仍然存在，自动恢复后可以采帧；不能把摄像头出图成功写成整板启动稳定性完全通过。**

## 环境与接线

- 用户摄像头标记：HBVCAM F2170HD V66。实际 USB VID/PID：**1E45:0209**；该 USB ID 不单独证明全部型号规格。
- 实测 ESP32-P4 v3.2、400MHz、16MiB Flash、32MiB PSRAM；Arduino IDE 2.3.10 的 CLI 1.5.1，Espressif core 3.3.12 / ESP-IDF 5.5.5。
- 用户确认 OV2710 排线已拔掉，HBVCAM 接开发板 USB-A，TTL 接 Mac，中间 USB-C 未接线；用户报告新 USB 摄像头接上不烫。
- 原生高速 USB Host + core 自带 UVC 驱动，未安装额外 Arduino 库。USB CDC On Boot Disabled，UART 115200，P4 下载开关。
- 构建：postv3、PSRAM enabled、16MB、DIO/40MHz、Huge APP、DebugLevel=info，额外 `build.img_freq=40m`。

## 编译、上传及实际图像

| 检查 | 结果 |
|---|---|
| 最终 640×480 程序编译 | 通过；436760 字节程序，24256 字节静态 RAM |
| 实机上传 | 通过；启动器、分区表、boot_app0、应用四项写入哈希验证成功 |
| 摄像头枚举 | 通过；地址 1，VID=0x1E45，PID=0x0209 |
| 描述符 | 读取到 22 个模式：11 个 MJPEG、11 个 YUY2；清单见 evidence/uvc_advertised_modes.csv |
| 初始低分辨率检查 | 160×120 MJPEG 约 30fps，保存并查看第一张照片 |
| 最终采帧 | 640×480 MJPEG；25 秒启动/采帧日志中有 19 条帧报告，总帧号从 30 至 582，实测 29.1–30.0fps |
| 最终采帧错误计数 | 上述记录中 usbErrors=0、drops=0、overflows=0；19 个不同采样 hash |
| JPEG 快照 | 640×480、16712 字节；字节数、JPEG SOI/EOI、协议尾部和 JPEG 内部 SOF 尺寸通过，实际打开可见现场画面 |
| 快照后恢复 | 再观察 12 秒，12 条帧报告，总帧号 3174 至 3511，30.0fps；USB 错误/丢帧/溢出均为 0 |

最终实测日志片段：

```text
[UVC_CONNECTED] address=1 VID=0x1E45 PID=0x0209
[UVC_OPEN] ESP_OK
[READY] MJPEG 640x480 nominalFPS=30.00
[FRAME] total=60 fps=30.0 size=18414 width=640 height=480 hash=56D09E33 usbErrors=0 drops=0 overflows=0 freePSRAM=31678964
```

照片 `hbvcam_640x480.jpg` SHA-256：

```text
56a4b2db300fcb3c5e7b61267914d7a6997428a251ec6686a3c0615b2a1c5ed1
```

这些是本机真实执行记录；README 中的省略号日志只是使用示例。UART 传输快照时程序主动暂停 USB 视频流，传输后恢复，因此不宣称快照期间也保持连续 30fps。

## 尚未解决或未验证

- 一次最终程序的 TTL RTS 复位记录再次出现启动器 `Load access fault`（PC 0x4ffb226c），看门狗自动重启后正常进入 UVC 应用。与先前开发板单独测试一致，根因尚未确定。没有在报告中省略这段异常。
- 未测试 USB 拔插供电冷启动、摄像头热插拔重连、长时间高负载或其他 ESP32 型号。
- 1920×1080 等高分辨率是摄像头描述符公布的能力，**没有实测 1080p 采帧**。本次实际验证的是 160×120 和最终 640×480。
- YUY2 模式只枚举，没有采集/解码；没有网络直播、屏幕显示或 C5 固件修改。
- OV2710 仍未验证，与这次 USB 摄像头成功出图分开记录。

本次完成了 Part B 的 Arduino 驱动结构、初始化、USB/UVC 视频流启动、借用帧处理、setup/loop 示例和实机 JPEG 证据。板上当前烧录的是这个 HBVCAM USB 程序。

## 证据位置

交付目录 `evidence/` 包括最终 compile/upload、完整启动/枚举日志、快照传输日志、快照后恢复日志、硬件汇总 JSON 和真实模式 CSV。JPEG 位于交付目录顶层。

项目原始日志、构建产物和汇总脚本保存在 `03_过程文件/partb_uvc/`；源码原件在 `04_代码与脚本/HBVCAM_USB_PartB/`。`hardware_summary.json` 明确区分 `camera_capture_verified=true` 与 `startup_stability_verified=false`。
