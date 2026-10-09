"""Source guards for a read-only diagnostic, not a UART/hardware simulation."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "baseChassisModule"
CHECK = (ROOT / "serialLinkDiagnostics.h").read_text()
BASE = (ROOT / "baseChassisModule.ino").read_text()


class ReceiveCheckContract(unittest.TestCase):
    def test_diagnostic_never_reconfigures_or_consumes(self):
        for forbidden in (".begin(", ".end(", ".read(", ".write(", ".overflow(",
                          "pinMode(", "digitalWrite(", "gpio_set_", "pio_sm_exec(",
                          "pio_sm_get(", "pio_sm_clear_", "irq_set_", "noInterrupts("):
            self.assertNotIn(forbidden, CHECK)

    def test_inspects_queue_and_hardware(self):
        for required in ("port.available()", "port.peek()", "pio_sm_get_rx_fifo_level",
                         "p->inte0", "irq_is_enabled", "p->sm[sm].addr"):
            self.assertIn(required, CHECK)

    def test_two_snapshots_without_blocking_wait(self):
        self.assertIn("receiveCheckRemaining = 2;", BASE)
        task = BASE.split("static void receiveCheckTask()", 1)[1].split("void loop()", 1)[0]
        self.assertIn("--receiveCheckRemaining;", task)
        self.assertIn("millis() + 1000", task)
        self.assertNotIn("delay(", task)
        self.assertIn("ARDUINO_PICO_VERSION_STR", task)
        self.assertIn("mastTaskCalls", task)


if __name__ == "__main__":
    result = unittest.main(argv=["test_receive_check_contract"], exit=False).result
    if not result.wasSuccessful():
        raise RuntimeError("Receive-check source contracts failed")
