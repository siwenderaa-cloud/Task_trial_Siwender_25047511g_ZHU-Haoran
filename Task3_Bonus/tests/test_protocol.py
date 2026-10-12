"""Binary framing checks for partial UART reads and corrupt camera transfers."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('serial_bridge', Path(__file__).resolve().parents[1] / 'bridge/serial_bridge.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ProtocolTest(unittest.TestCase):
    def packet(self, jpeg=b'\xff\xd8test\x00\n\xff\xd9'):
        return b'JPEG %d 640 480\n' % len(jpeg) + jpeg + module.TRAILER

    def test_every_possible_split(self):
        packet = b'[FRAME] total=1\r\n' + self.packet()
        for split in range(len(packet) + 1):
            p = module.FrameParser()
            events = p.feed(packet[:split]) + p.feed(packet[split:])
            frames = [e for e in events if e['type'] == 'frame']
            self.assertEqual(len(frames), 1)
            self.assertEqual((frames[0]['width'], frames[0]['height']), (640, 480))
            self.assertEqual(frames[0]['jpeg'], b'\xff\xd8test\x00\n\xff\xd9')

    def test_multiple_frames_and_byte_at_a_time(self):
        p = module.FrameParser()
        events = []
        for value in self.packet() * 3:
            events.extend(p.feed(bytes([value])))
        self.assertEqual(len([e for e in events if e['type'] == 'frame']), 3)

    def test_size_limit(self):
        for line in [b'JPEG 1048577 640 480\n', b'JPEG 100 0 480\n', b'JPEG abc 640 480\n', b'JPEG 100 640\n']:
            with self.assertRaises(ValueError):
                module.FrameParser().feed(line)

    def test_corrupt_trailer(self):
        with self.assertRaises(ValueError):
            module.FrameParser().feed(self.packet()[:-1] + b'x')

    def test_corrupt_jpeg(self):
        with self.assertRaises(ValueError):
            module.FrameParser().feed(self.packet(b'bad-data'))

    def test_bounded_logs(self):
        with self.assertRaises(ValueError):
            module.FrameParser().feed(b'x' * 8193)


if __name__ == '__main__':
    unittest.main()
