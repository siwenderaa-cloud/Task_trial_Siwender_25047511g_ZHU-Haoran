#!/usr/bin/env python3
"""Persistent, standard-library POSIX UART bridge. stdout is NDJSON for Node.

Reuse the existing firmware's j / JPEG <bytes> <width> <height> / END_JPEG
protocol. Only a fully validated JPEG is emitted. Stop drains an in-flight
transfer, then pauses host acquisition; firmware background capture is unchanged.
"""
import argparse
import array
import base64
import fcntl
import glob
import json
import os
import select
import sys
import termios
import time

TRAILER = b'\nEND_JPEG\n'


class FrameParser:
    def __init__(self):
        self.pending = bytearray()
        self.expected = None

    def feed(self, data):
        self.pending.extend(data)
        events = []
        while True:
            if self.expected is not None:
                size, width, height = self.expected
                if len(self.pending) < size + len(TRAILER):
                    break
                jpeg = bytes(self.pending[:size])
                trailer = bytes(self.pending[size:size + len(TRAILER)])
                del self.pending[:size + len(TRAILER)]
                self.expected = None
                if trailer != TRAILER or not jpeg.startswith(b'\xff\xd8') or not jpeg.endswith(b'\xff\xd9'):
                    raise ValueError('Incomplete or corrupt JPEG transfer. Stop and restart the stream.')
                events.append({'type': 'frame', 'width': width, 'height': height, 'jpeg': jpeg})
                continue
            if b'\n' not in self.pending:
                if len(self.pending) > 8192:
                    raise ValueError('Serial log exceeded the protocol limit.')
                break
            line, _, tail = self.pending.partition(b'\n')
            self.pending = bytearray(tail)
            line = line.rstrip(b'\r')
            if line.startswith(b'JPEG '):
                fields = line.split()
                if len(fields) != 4:
                    raise ValueError('Invalid camera frame header.')
                size, width, height = map(int, fields[1:])
                if not (4 <= size <= 1048576 and 0 < width <= 4096 and 0 < height <= 4096):
                    raise ValueError('Camera frame size is out of bounds.')
                self.expected = (size, width, height)
            elif line:
                events.append({'type': 'log', 'message': line.decode('utf-8', errors='replace')[:400]})
        return events


def emit(event):
    print(json.dumps(event, separators=(',', ':')), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port')
    args = ap.parse_args()
    ports = sorted(set(glob.glob('/dev/cu.usb*') + glob.glob('/dev/cu.wch*')))
    port = args.port or (ports[0] if len(ports) == 1 else None)
    if not port:
        raise RuntimeError('Connect TTL to your Mac. If several serial devices exist, set SERIAL_PORT. Close Arduino Serial Monitor.')
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        # Keep one owner and leave auto-reset lines deasserted. No reset is requested.
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if hasattr(termios, 'TIOCEXCL'):
            fcntl.ioctl(fd, termios.TIOCEXCL)
        attrs = termios.tcgetattr(fd)
        attrs[0] = attrs[1] = attrs[3] = 0
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[4] = attrs[5] = termios.B115200
        attrs[6][termios.VMIN] = attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        fcntl.ioctl(fd, termios.TIOCMBIC, array.array('i', [termios.TIOCM_DTR | termios.TIOCM_RTS]))
        termios.tcflush(fd, termios.TCIFLUSH)
        emit({'type': 'open', 'port': port})
        active = False
        waiting = False
        request_at = 0
        last_complete = 0
        command_buffer = bytearray()
        parser = FrameParser()
        while True:
            now = time.monotonic()
            if active and not waiting and now - last_complete >= 0.4:
                os.write(fd, b'j\n')
                waiting = True
                request_at = now
            if waiting and now - request_at > 20:
                if parser.expected is not None:
                    raise TimeoutError('Camera JPEG transfer timed out. Check TTL cable and firmware.')
                # The board may be rebooting. Retry requests while it recovers.
                waiting = False
                if now - last_complete > 45:
                    raise TimeoutError('No camera frame received. Check USB-A camera, firmware and Serial Monitor.')
            readable, _, _ = select.select([fd, sys.stdin.fileno()], [], [], 0.2)
            if sys.stdin.fileno() in readable:
                commands = os.read(sys.stdin.fileno(), 4096)
                if not commands:
                    return
                command_buffer.extend(commands)
                while b'\n' in command_buffer:
                    line, _, tail = command_buffer.partition(b'\n')
                    command_buffer = bytearray(tail)
                    action = line.strip()
                    if action == b'quit':
                        return
                    if action == b'start':
                        active = True
                        last_complete = time.monotonic()
                    elif action == b'stop':
                        active = False
            if fd in readable:
                chunk = os.read(fd, 65536)
                if not chunk:
                    raise ConnectionError('Serial device disconnected. Reconnect TTL and press Start stream.')
                for event in parser.feed(chunk):
                    if event['type'] == 'frame':
                        waiting = False
                        last_complete = time.monotonic()
                        if active:
                            event['data'] = base64.b64encode(event.pop('jpeg')).decode('ascii')
                            emit(event)
                    else:
                        emit(event)
    finally:
        if hasattr(termios, 'TIOCNXCL'):
            try:
                fcntl.ioctl(fd, termios.TIOCNXCL)
            except OSError:
                pass
        os.close(fd)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        emit({'type': 'error', 'message': str(error)})
        sys.exit(1)
