"""Source guards only, not a hardware/UART simulation. Safe to run in Spyder."""
from pathlib import Path
import unittest

root = Path(__file__).resolve().parents[1]
base = (root / 'baseChassisModule/baseChassisModule.ino').read_text()
display = (root / 'displayModule_v6_gem/displayModule_v6_gem.ino').read_text()


class DisplayStartupContract(unittest.TestCase):
    def test_original_pins_and_first_baud(self):
        self.assertIn('SerialPIO dispSerial(5, 4, 256);', base)
        self.assertIn('static uint32_t displayBaud = 38400;', base)
        self.assertIn('displayBaud == 38400 ? 460800 : 38400', base)

    def test_established_link_is_not_retimed(self):
        task = base.split('static void displayLinkTask()', 1)[1].split('static void sendCfgText', 1)[0]
        self.assertLess(task.index('if (displayBaudConfirmed) return;'), task.index('dispSerial.end();'))
        self.assertNotIn('mastSerial.', task)
        self.assertNotIn('keysSerial.', task)
        self.assertIn('now - displayBaudStartedMs >= 3000', task)
        self.assertIn('now - displayBaudProbeMs >= 250', task)

    def test_exact_existing_protocol_challenge(self):
        receiver = base.split('void displayRxTask()', 1)[1].split('if (!strcmp(key, "MESHACK"))', 1)[0]
        self.assertIn('!strcmp(key, "PONG")', receiver)
        self.assertIn('!strcmp(valueText, displayBaudToken)', receiver)
        pong = display.split('if (!strcmp(key, "PING"))', 1)[1].split('return;', 1)[0]
        self.assertIn('baseSerial.print("@PONG,");', pong)
        self.assertIn('baseSerial.println(valueText);', pong)

    def test_restore_and_mesh_wait_for_confirmation(self):
        restore = base.split('static bool restoreSavedGemForDisplay()', 1)[1].split('void displayRxTask()', 1)[0]
        self.assertLess(restore.index('if (!displayBaudConfirmed) return false;'), restore.index('lastGemSdIndex();'))
        sender = base.split('static void sendActiveMesh()', 1)[1].split('static void meshTransferTask()', 1)[0]
        self.assertIn('if (!displayBaudConfirmed) { displayMeshRequested = true; return; }', sender)
        transfer = base.split('static void meshTransferTask()', 1)[1]
        self.assertLess(transfer.index('if (!displayBaudConfirmed) return;'), transfer.index('if (!meshSendStage) return;'))

    def test_usb_after_original_reset(self):
        setup = base.split('void setup()', 1)[1].split('void loop()', 1)[0]
        self.assertLess(setup.index('initSteppers();'), setup.index('rp2040.restart();'))
        self.assertLess(setup.index('rp2040.restart();'), setup.index('Serial.begin(115200);'))
        self.assertLess(setup.index('Serial.begin(115200);'), setup.index('delay(500);'))


if __name__ == '__main__':
    result = unittest.main(argv=['test_display_startup_contract'], exit=False).result
    if not result.wasSuccessful():
        raise RuntimeError('Display startup source contracts failed')
