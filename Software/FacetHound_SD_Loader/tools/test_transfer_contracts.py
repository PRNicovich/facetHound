"""Source-contract checks (not a UART/hardware simulation); Python/Spyder safe."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = (ROOT / "baseChassisModule/baseChassisModule.ino").read_text()
DISPLAY = (ROOT / "displayModule_v6_gem/displayModule_v6_gem.ino").read_text()
MOTORS = (ROOT / "baseChassisModule/motorControl.cpp").read_text()


class TransferContracts(unittest.TestCase):
    def test_final_ack_before_redraw(self):
        body = DISPLAY.split("if (runtimeGemMesh().finishTransfer())", 1)[1]
        self.assertLess(body.index('sendLineBoth("@MESHACK,READY")'),
                        body.index("closeSettingsMenu()"))

    def test_uart_not_blocked_by_usb_mirror(self):
        body = DISPLAY.split("static void sendLineBoth(const char* line)\n{", 1)[1]
        body = body.split("\n}", 1)[0]
        self.assertLess(body.index("baseSerial.println"), body.index("Serial.println"))
        self.assertIn("Serial.availableForWrite()", body)

    def test_recovery_preserves_cache_identity(self):
        body = BASE.split("static void retryMeshTransfer", 1)[1].split("static void sendActiveMesh", 1)[0]
        self.assertIn("meshSendStage = 9", body)
        self.assertIn("sendDisplayLine(meshCacheRequest)", body)
        self.assertIn("++meshRestartCount <= 2", body)
        self.assertIn('"BEGIN acknowledgment timeout"', BASE)
        self.assertIn('"READY acknowledgment timeout"', BASE)

    def test_ready_retransmission_is_bounded(self):
        self.assertIn("meshEndAttempts < 3", BASE)
        self.assertIn('sendDisplayLine("@MESHEND,1")', BASE)
        self.assertIn('if (!meshReceiving && runtimeGemMesh().active()) { sendLineBoth("@MESHACK,READY"); return; }', DISPLAY)

    def test_pump_test_independent_of_gem(self):
        body = BASE.split('if (!strcasecmp(mode, "STEPTEST"))', 1)[1].split('if (!strcasecmp(mode, "REINIT"))', 1)[0]
        self.assertNotIn("meshSendStage", body)
        self.assertIn("S.RPMValue>10", body)
        self.assertIn("hardStopTwist(S)", body)
        self.assertIn("hardStopZ()", body)
        self.assertIn("startPumpStepTest(S)", body)

    def test_normal_pump_uses_step_dir(self):
        body = MOTORS.split("static void updatePump(SystemState &S)", 1)[1].split("void updateMotors", 1)[0]
        self.assertIn("pumpDriver.moveUsingStepDirInterface()", body)
        self.assertIn("add_repeating_timer_us", body)
        self.assertNotIn("moveAtVelocity(int32_t(flow)", body)
        self.assertIn("if(flow>750) flow=750", body)
        self.assertIn("@FAULT,PUMP,STEP_TIMER_UNAVAILABLE", body)

    def test_pump_pause_cancels_pulses_before_return(self):
        body = MOTORS.split("// The installed pump moves", 1)[1].split("void updateMotors", 1)[0]
        self.assertLess(body.index("stopPumpPulses()"), body.index("flow<=0) return"))
        self.assertLess(body.index("digitalWrite(PUMP_EN_PIN,HIGH)"), body.index("flow<=0) return"))


if __name__ == "__main__":
    result = unittest.main(argv=["test_transfer_contracts"], exit=False).result
    if not result.wasSuccessful():
        raise RuntimeError("Firmware source contracts failed")
