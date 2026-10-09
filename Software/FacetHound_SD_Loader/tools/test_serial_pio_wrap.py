"""Reproduce core 4.5.2's 32-bit ring arithmetic; safe to run in Spyder.

Source: arduino-pico tag 4.5.2, cores/rp2040/SerialPIO.cpp and .h.
This models its queue operations, not electrical UART sampling.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "baseChassisModule"


def old_available(writer, reader, allocation):
    return ((writer - reader) & 0xFFFFFFFF) % allocation


class OldRing:
    def __init__(self, usable):
        self.size = usable + 1
        self.reader = self.writer = 0
        self.data = [None] * self.size

    def write(self, byte):
        next_writer = (self.writer + 1) % self.size
        if next_writer == self.reader:
            return False
        self.data[self.writer] = byte
        self.writer = next_writer
        return True

    def available(self):
        return old_available(self.writer, self.reader, self.size)

    def peek(self):
        return self.data[self.reader] if self.writer != self.reader else -1

    def read(self):
        if self.writer == self.reader:
            return -1
        byte = self.data[self.reader]
        self.reader = (self.reader + 1) % self.size
        return byte


class SerialPioWrap(unittest.TestCase):
    def test_reproduces_reported_deadlock(self):
        q = OldRing(256)
        q.write(0)
        q.read()  # Nonzero reader makes the next full queue wrap.
        for _ in range(256):
            self.assertTrue(q.write(ord('@')))
        self.assertEqual(q.available(), 0)
        self.assertEqual(q.peek(), 64)
        self.assertFalse(q.write(ord('t')))

    def test_every_cursor_pair_for_corrected_sizes(self):
        for allocation in (32, 128, 256):
            for reader in range(allocation):
                for writer in range(allocation):
                    self.assertEqual(old_available(writer, reader, allocation),
                                     (writer-reader) % allocation)

    def test_repeated_full_wrapped_queues_recover_in_order(self):
        for usable in (31, 127, 255):
            q = OldRing(usable)
            q.write(0)
            q.read()
            for _ in range(20):
                expected = [i & 255 for i in range(usable)]
                for byte in expected:
                    self.assertTrue(q.write(byte))
                self.assertFalse(q.write(99))  # Full queue must not overwrite.
                actual = []
                while q.available():
                    actual.append(q.read())
                self.assertEqual(actual, expected)
                self.assertEqual(q.peek(), -1)

    def test_firmware_requests_match_tested_allocations(self):
        base = (ROOT / 'baseChassisModule.ino').read_text()
        motor = (ROOT / 'motorControl.cpp').read_text()
        self.assertIn('KEYBOARD_UART_SWAP_TRIAL ? 7 : 6, 127)', base)
        self.assertIn('SerialPIO dispSerial(5, 4, 255);', base)
        self.assertIn('MAST_UART_SWAP_TRIAL ? 3 : 2, 255)', base)
        self.assertIn('lapMotorSerial(LAP_MOTOR_TX_PIN, LAP_MOTOR_RX_PIN, 31)', motor)


if __name__ == '__main__':
    result = unittest.main(argv=['test_serial_pio_wrap'], exit=False).result
    if not result.wasSuccessful():
        raise RuntimeError('SerialPIO wrap regression failed')
