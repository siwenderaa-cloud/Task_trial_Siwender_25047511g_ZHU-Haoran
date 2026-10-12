# Browser camera demo verification

Verified on 2026-10-12 in Asia/Hong_Kong, using the user's actual HBVCAM USB camera and ESP32-P4. This report describes the Bonus browser path separately from the previous board-only 30 fps Part B result.

## Actual result

Real camera image → ESP32-P4 USB Host → 115200 baud TTL UART → Python bridge → Node.js HTTP MJPEG → browser is working at **640×480**, approximately **0.4–0.5 browser frames per second** in the observed scene. A five-frame MJPEG sample contains five different SHA-256 hashes and independently parsed JPEG SOF dimensions of 640×480. Browser screenshots show actual camera output; no static example image was used as a feed.

Environment: macOS Apple Silicon, v24.19.0, Python 3.9.6; existing Arduino HBVCAM Part B firmware unchanged. The backend binds only to 127.0.0.1:4174. No cloud deployment or new npm dependencies.

## Passed checks

| Check | Evidence |
| --- | --- |
| HTTP 200 and correct page title | `evidence/hardware_web_checks.json` |
| Complete real multipart MJPEG output | Five different JPEGs and hashes in the same JSON; `browser_feed_frame_01.jpg` … `05.jpg` |
| Stop pauses host acquisition | Sequence unchanged for 4 seconds; browser shows Paused and retains the last frame |
| Start after Stop | New real frames arrive; mouse and keyboard activation both exercised |
| Save current frame | HTTP JPEG attachment and actual browser download; `browser_saved_frame.jpg` |
| Missing serial input | Isolated server with unavailable serial path reports Disconnected and zero frames; no image substituted |
| Server goes offline | Browser displays Server disconnected / last received frame and disables controls |
| Backend restarts | Previous image clears, placeholder returns, Save disabled until a new frame arrives |
| Desktop and narrow-screen layout | 1280×900 and 390×844 viewports; no horizontal overflow; natural image 640×480 |
| Mobile scrolling and keyboard focus | Controls remain reachable, visible solid focus ring, Enter starts capture |
| Protocol parser | Six unit tests pass, including every packet split, byte-at-a-time input, malformed sizes and corrupt JPEG/trailer |
| JavaScript syntax | Both server and frontend pass Node syntax checks |
| Local control origin | Cross-origin POST rejected with HTTP 403 |

The final desktop screenshot shows a running session; the paused screenshot shows a stopped stream. These are different moments from a real session. Browser fps is measured from UART snapshots and is not the board's UVC fps.

## Boundaries and unfinished items

- The existing board hardware-reset bootloader fault remains unresolved. This Bonus version does not reflash or intentionally reset the board.
- No physical hot-unplug/replug, cold-power startup, Windows runtime, other browser engines, prolonged unattended stability, or higher UART baud was verified. The missing-port error path was tested without unplugging hardware.
- Reduced-motion CSS was inspected and is present; the operating system reduced-motion setting was not changed during testing. There are no looping animations.
- Stop pauses host acquisition/forwarding. The unchanged board firmware can continue background UVC capture.
- AI links in `AI_CONVERSATIONS.md` are local Codex references. Public AI share URLs and GitHub publication were not created.
- QA servers are stopped at delivery to release the serial port. Start the app with `python3 run.py` or `start.command` for a new session.

## Evidence files

See `evidence/hardware_web_checks.json`, `browser_ui_checks.json`, unit/syntax logs, real JPEGs, and desktop/mobile/paused browser screenshots. The earlier Part B firmware, its original verification and available evidence are retained under `firmware/HBVCAM_USB_PartB/`.
