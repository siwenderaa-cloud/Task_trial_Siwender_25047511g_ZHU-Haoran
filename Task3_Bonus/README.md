# HBVCAM camera stream to browser

A small complete full-stack demo: a real USB camera feeds ESP32-P4, the Mac backend receives JPEG snapshots over TTL UART, and a browser displays an MJPEG image feed. It runs locally and offline. No cloud server, npm packages, database, or frontend build is required.

## Quick start on this Mac

1. Keep the verified wiring: HBVCAM → development-board USB-A, board TTL → Mac, selector → P4. Keep the previous OV2710 cable disconnected and the middle USB-C empty as in the verified Part B setup.
2. Close Arduino Serial Monitor and other serial readers. Keep the existing HBVCAM Part B firmware on the board; this app does not reflash it.
3. Open Terminal in this folder and run:

```sh
python3 run.py
```

The launcher uses installed Node.js, or this Mac's existing Codex bundled Node.js. Alternatively, double-click `start.command` on macOS. A computer with Node.js 20+ and Python 3 can use:

```sh
npm run start
```

No `npm install` is needed. Open **http://127.0.0.1:4174** and press **Start stream**. Press **Stop stream** to pause acquisition and keep the last frame. **Save current frame** downloads the last complete JPEG. Press Ctrl+C in Terminal to stop the server and release the serial port.

This project uses 4174 because 4173 was already used by another local project. A second browser tab shares the same acquisition worker.

## 中文运行说明

在本目录运行 `python3 run.py`，打开 `http://127.0.0.1:4174`，点击 **Start stream**。也可双击 `start.command`。停止画面用 **Stop stream**；关闭整个服务在终端按 Ctrl+C。请先关闭 Arduino 串口监视器。无需购买服务器、联网或安装 npm 依赖。

## Configuration

The bridge auto-detects a unique `/dev/cu.usb*` or `/dev/cu.wch*` serial device. If more than one is connected, select the actual TTL port:

```sh
SERIAL_PORT=/dev/cu.usbmodemXXXX python3 run.py
PORT=4175 python3 run.py
```

`PYTHON` and `NODE` can select installed runtimes. `run.py` uses the same Python executable for the worker. The server binds only to `127.0.0.1`; this version is a local Mac demo. The Python serial bridge uses POSIX `termios` and is not Windows-compatible. Windows execution has not been verified.

## What the controls mean

- Start opens a serial connection and repeatedly requests a fresh JPEG using the existing firmware's `j` command.
- Stop stops further host snapshot requests, drains any transfer in progress, and ends browser MJPEG streams. It leaves the last frame visible, clearly marked Paused. The board's original background UVC capture may continue; Stop does not switch off camera power or change the firmware.
- Connected is shown only after a recent complete camera frame arrives. Waiting, disconnected, paused and server-offline states are distinct. Old images are labeled as the last received frame.
- Browser feed fps is measured from received UART snapshots. It is different from the previously measured 30 fps UVC acquisition on the board.

## Architecture

```text
HBVCAM --USB UVC--> ESP32-P4 --TTL UART JPEG--> Python bridge
                                                     |
                                          NDJSON / complete frames
                                                     |
                                           Node.js HTTP backend
                                                     |
                                       MJPEG stream --> HTML + JS
```

The feed is acquired at runtime from the attached camera. No sample photo is embedded in the page or substituted for the live feed. `dist/` contains only HTML, CSS and JavaScript.

| File | Purpose |
| --- | --- |
| `server.mjs` | Local server, start/stop/status/frame API, MJPEG delivery, slow-viewer frame dropping |
| `bridge/serial_bridge.py` | Persistent exclusive UART owner; fragmented-read parser; bounded JPEG validation |
| `dist/index.html`, `style.css`, `app.js` | Responsive page, controls, connection state, frame count and download |
| `run.py`, `start.command` | Start using local runtimes |
| `firmware/HBVCAM_USB_PartB/` | Exact existing Arduino sketch and adapter source, included for reproducibility |
| `tests/test_protocol.py` | Binary parser checks for partial reads, corrupt packets and size limits |
| `VERIFICATION.md`, `evidence/` | Actual checks, browser screenshots and camera evidence |
| `AI_CONVERSATIONS.md` | AI conversation references |

## API

| Method | Path | Result |
| --- | --- | --- |
| GET | `/` | Browser page |
| POST | `/api/start` | Start host acquisition; return state |
| POST | `/api/stop` | Pause host acquisition; return state |
| GET | `/api/status` | Connection, frame age, received fps, dimensions and count |
| GET | `/stream` | `multipart/x-mixed-replace` JPEG stream; start first |
| GET | `/api/frame?download=1` | Download the latest complete real JPEG |

## Checks

```sh
python3 -m unittest discover -s tests -v
node --check server.mjs
node --check dist/app.js
```

Read `VERIFICATION.md` for what was actually verified. Successful tests alone do not prove a real camera feed.

## Limitations and troubleshooting

The unchanged firmware uses 115200 baud. The board pauses UVC while it transmits each snapshot and resumes afterward. This is a near-live image feed, with visibly lower frame rate than the board's 30 fps acquisition. Image size and scene detail change transmission time. This demo prioritizes a complete hardware-to-browser path over smooth video.

If no picture arrives: close Serial Monitor, check the TTL port, confirm the USB-A camera and current Part B firmware, then press Stop and Start. The known board bootloader reset fault remains unresolved. This application does not intentionally toggle reset, but opening a serial port may still trigger the adapter's reset behavior; allow the board to recover. A serial read failure stops acquisition and shows Disconnected. Reconnect, wait briefly, then press Start again. Physical hot-plug behavior is only claimed if listed as tested in VERIFICATION.md.

Future work: higher verified UART baud or a native network transport, background continuous JPEG transfer, more complete reconnect handling, and longer-duration stability checks. ESP32-P4/C5 Wi-Fi integration is outside this version.

## References and reuse

- [Node.js HTTP](https://nodejs.org/api/http.html): built-in streaming responses.
- [Python termios](https://docs.python.org/3/library/termios.html): standard-library POSIX serial configuration.
- [Espressif Arduino USB Host](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/usb_host.html).
- Existing project firmware references and verification are retained under `firmware/HBVCAM_USB_PartB/`.

Node/Python/browser code in this folder was authored for this demo. It uses standard runtimes and no third-party packages. Firmware uses the installed Espressif driver; that dependency and upstream licensing remain described in its README. No GitHub repository or public conversation share was created.
