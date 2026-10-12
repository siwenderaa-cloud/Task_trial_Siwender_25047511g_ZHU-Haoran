#!/usr/bin/env python3
"""macOS/POSIX: read one real JPEG snapshot from HBVCAM_USB_PartB over TTL UART.
Uses the Python standard library. Close Arduino Serial Monitor first.
Example: python3 capture_hbvcam.py --port /dev/cu.usbmodemXXXX --output photo.jpg
"""
import argparse
import array
import fcntl
import glob
import os
from pathlib import Path
import select
import termios
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', help='TTL USB serial device; auto-detect if unique')
    ap.add_argument('--output', type=Path, default=Path('hbvcam_snapshot.jpg'))
    ap.add_argument('--monitor', type=float, default=0, help='Print serial logs for N seconds instead of saving JPEG')
    ap.add_argument('--reset', action='store_true', help='Reset P4 using TTL RTS before capturing startup logs')
    args = ap.parse_args()
    ports = sorted(glob.glob('/dev/cu.usb*') + glob.glob('/dev/cu.wch*'))
    port = args.port or (ports[0] if len(ports) == 1 else None)
    if not port:
        ap.error('Choose --port; detected USB ports: ' + repr(ports))
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attrs = termios.tcgetattr(fd)
        attrs[0] = attrs[1] = attrs[3] = 0
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[4] = attrs[5] = termios.B115200
        attrs[6][termios.VMIN] = attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        # Deassert UART auto-reset lines. Opening the port can cause a reboot;
        # tolerate this and retry the 'j' command until the firmware is ready.
        fcntl.ioctl(fd, termios.TIOCMBIC, array.array('i', [termios.TIOCM_DTR | termios.TIOCM_RTS]))
        termios.tcflush(fd, termios.TCIFLUSH)
        if args.reset:
            fcntl.ioctl(fd, termios.TIOCMBIS, array.array('i', [termios.TIOCM_RTS]))
            time.sleep(0.15)
            termios.tcflush(fd, termios.TCIFLUSH)
            fcntl.ioctl(fd, termios.TIOCMBIC, array.array('i', [termios.TIOCM_RTS]))
        deadline = time.monotonic() + (args.monitor or 180)
        pending = bytearray()
        size = None
        last_command = 0
        while time.monotonic() < deadline:
            now = time.monotonic()
            if not args.monitor and size is None and now - last_command > 5:
                os.write(fd, b'j\n')
                last_command = now
            readable, _, _ = select.select([fd], [], [], 0.5)
            if not readable:
                continue
            chunk = os.read(fd, 65536)
            if args.monitor:
                print(chunk.decode('utf-8', errors='replace'), end='', flush=True)
                continue
            pending.extend(chunk)
            if size is None:
                while b'\n' in pending:
                    line, _, tail = pending.partition(b'\n')
                    pending = bytearray(tail)
                    if line.startswith(b'JPEG '):
                        fields = line.split()
                        if len(fields) != 4:
                            raise RuntimeError('Invalid JPEG header: ' + repr(line))
                        size, width, height = map(int, fields[1:])
                        if not (0 < size <= 1024 * 1024 and 0 < width <= 4096 and 0 < height <= 4096):
                            raise RuntimeError('Invalid JPEG size/dimensions')
                        print(f'Receiving {width}x{height}, {size} bytes from {port} ...', flush=True)
                        break
                    if line:
                        print(line.decode('utf-8', errors='replace'), flush=True)
            if size is not None and len(pending) >= size + len(b'\nEND_JPEG\n'):
                data = bytes(pending[:size])
                trailer = bytes(pending[size:size + len(b'\nEND_JPEG\n')])
                if trailer != b'\nEND_JPEG\n' or not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9'):
                    raise RuntimeError('JPEG transfer corrupt/incomplete; retry and check UART logs')
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_bytes(data)
                print('Saved ' + str(args.output.resolve()))
                return
        if not args.monitor:
            raise TimeoutError('No complete JPEG received in 180 seconds. Check camera logs with --monitor 15.')
    finally:
        os.close(fd)


if __name__ == '__main__':
    main()
