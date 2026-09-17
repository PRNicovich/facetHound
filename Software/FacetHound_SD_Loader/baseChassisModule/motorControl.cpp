#include "motorControl.h"
#include <math.h>

AccelStepper twistDirStep(AccelStepper::DRIVER, TWIST_STEP_PIN, TWIST_DIR_PIN);
AccelStepper zedDirStep  (AccelStepper::DRIVER, ZED_STEP_PIN,   ZED_DIR_PIN);

static SerialPIO twistSerial(TWIST_TX_PIN, 0xff);
static SerialPIO zedSerial  (ZED_TX_PIN, 0xff);
static SerialPIO pumpSerial (PUMP_TX_PIN, 0xff);
#if USE_LAP_MOTOR_RS485
static SerialPIO lapMotorSerial(LAP_MOTOR_TX_PIN, LAP_MOTOR_RX_PIN);
#endif

TMC2209 twistDriver;
TMC2209 zedDriver;
TMC2209 pumpDriver;

float tiltStepsPerIndexUnit = 0.0f;
float tiltConversion        = 0.0f;
float zConversionFactor     = ZED_INDEX_INITS_PER_TURN /
                              (ZED_GEAR_RATIO * ZED_ENC_OVERSAMPLE);

int zMult[3] = {
    int(lroundf(Z_STEPS_PER_MM)),
    int(lroundf(0.0500f * Z_STEPS_PER_MM)),
    int(lroundf(0.0005f * Z_STEPS_PER_MM))
};

static int  nLocks = 0;
static bool twistSettled = false;
static bool twistMoving = false;
static bool zMoving = false;
static float lastTwistTarget = NAN;
static uint32_t lastTwistControlMs = 0;

static int lastEscDir = -1;
static int lastEscRpm = -1;
static int lastPumpDir = -1;
static int lastPumpFlow = -1;
static bool twistDriverIsEnabled = false;
static bool zedDriverIsEnabled = false;
static LapMotorDiagnostics lapDiagnostics;

#if USE_LAP_MOTOR_RS485
static const uint16_t REG_CONTROL      = 0x8000;
static const uint16_t REG_COMMAND_RPM  = 0x8005;
static const uint16_t REG_ACTUAL_SPEED = 0x8018;
static const uint8_t CONTROL_FORWARD   = 0x09;
static const uint8_t CONTROL_REVERSE   = 0x0B;
static const uint8_t CONTROL_BRAKE     = 0x0D;

static uint8_t lapReply[8];
static uint8_t lapReplyLength = 0;
static uint8_t lapExpectedLength = 0;
static uint8_t lapPhase = 0;
static uint32_t lapRequestStartedMs = 0;
static uint32_t lapLastFrameMs = 0;
static int lapSentRpm = -1;
static uint8_t lapSentControl = 0xff;
static bool lapProbeRequested = false;

static void rememberLapReply(uint8_t length)
{
    lapDiagnostics.lastReplyLength = min(length, uint8_t(sizeof(lapDiagnostics.lastReply)));
    for (uint8_t i = 0; i < lapDiagnostics.lastReplyLength; ++i)
        lapDiagnostics.lastReply[i] = lapReply[i];
}

static uint16_t modbusCRC(const uint8_t* data, size_t length)
{
    uint16_t crc = 0xffff;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];

        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 1) ? ((crc >> 1) ^ 0xa001) : (crc >> 1);
    }

    return crc;
}

static void sendModbusFrame(uint8_t* frame, uint8_t length, uint8_t expectedReply)
{
    uint16_t crc = modbusCRC(frame, length);
    frame[length++] = uint8_t(crc);
    frame[length++] = uint8_t(crc >> 8);

    while (lapMotorSerial.available())
        lapMotorSerial.read();

    lapMotorSerial.write(frame, length);
    lapDiagnostics.txFrames++;
    lapReplyLength = 0;
    lapExpectedLength = expectedReply;
    lapRequestStartedMs = millis();
    lapLastFrameMs = lapRequestStartedMs;
}

static void writeLapRegister(uint16_t address, uint8_t valueHigh, uint8_t valueLow)
{
    uint8_t frame[8] = {
        LAP_MOTOR_SLAVE_ID, 0x06,
        uint8_t(address >> 8), uint8_t(address), valueHigh, valueLow, 0, 0
    };
    sendModbusFrame(frame, 6, 8);
}

static void readLapActualSpeed()
{
    uint8_t frame[8] = {
        LAP_MOTOR_SLAVE_ID, 0x03,
        uint8_t(REG_ACTUAL_SPEED >> 8), uint8_t(REG_ACTUAL_SPEED), 0x00, 0x01, 0, 0
    };
    sendModbusFrame(frame, 6, 7);
}

static bool collectLapReply(SystemState &S)
{
    while (lapMotorSerial.available() && lapReplyLength < sizeof(lapReply))
    {
        lapReply[lapReplyLength++] = uint8_t(lapMotorSerial.read());
        lapDiagnostics.rxBytes++;
        lapDiagnostics.lastRxByteMs = millis();
    }

    // A Modbus exception is always slave, function|0x80, exception, CRC16.
    if (lapReplyLength >= 5 && lapReply[0] == LAP_MOTOR_SLAVE_ID &&
        (lapReply[1] & 0x80u))
    {
        const uint16_t receivedCRC = uint16_t(lapReply[3]) |
                                     (uint16_t(lapReply[4]) << 8);
        rememberLapReply(5);
        lapDiagnostics.lastFunction = lapReply[1];
        lapDiagnostics.lastException = lapReply[2];
        if (receivedCRC == modbusCRC(lapReply, 3))
        {
            lapDiagnostics.validReplies++;
            lapDiagnostics.exceptions++;
            lapDiagnostics.lastValidReplyMs = millis();
        }
        else
        {
            lapDiagnostics.crcErrors++;
        }
        lapExpectedLength = 0;
        lapPhase = 0;
        return true;
    }

    if (lapReplyLength < lapExpectedLength)
    {
        if (millis() - lapRequestStartedMs <= 60)
            return false;

        lapDiagnostics.timeouts++;
        rememberLapReply(lapReplyLength);

        if (lapPhase == 1)
            lapSentRpm = -1;
        else if (lapPhase == 2)
            lapSentControl = 0xff;

        lapExpectedLength = 0;
        lapPhase = 0;
        return true;
    }

    uint16_t receivedCRC = uint16_t(lapReply[lapExpectedLength - 2]) |
                           (uint16_t(lapReply[lapExpectedLength - 1]) << 8);

    bool validCRC = receivedCRC == modbusCRC(lapReply, lapExpectedLength - 2);
    bool validWriteEcho = validCRC && lapExpectedLength == 8 &&
                          lapReply[0] == LAP_MOTOR_SLAVE_ID && lapReply[1] == 0x06;

    rememberLapReply(lapExpectedLength);
    lapDiagnostics.lastFunction = lapReply[1];
    lapDiagnostics.lastException = 0;

    if (!validCRC)
        lapDiagnostics.crcErrors++;

    if (validCRC &&
        lapReply[0] == LAP_MOTOR_SLAVE_ID && lapReply[1] == 0x03 &&
        lapExpectedLength == 7 && lapReply[2] == 2)
    {
        uint16_t rawSpeed = (uint16_t(lapReply[3]) << 8) | lapReply[4];
        S.RPMValue = int((uint32_t(rawSpeed) * 20U) / LAP_MOTOR_POLE_PAIRS);
        lapDiagnostics.validReplies++;
        lapDiagnostics.readReplies++;
        lapDiagnostics.lastValidReplyMs = millis();
    }
    else if (validWriteEcho)
    {
        lapDiagnostics.validReplies++;
        lapDiagnostics.writeAcks++;
        lapDiagnostics.lastValidReplyMs = millis();
    }
    else if ((lapPhase == 1 || lapPhase == 2) && !validWriteEcho)
    {
        if (lapPhase == 1)
            lapSentRpm = -1;
        else
            lapSentControl = 0xff;
        lapDiagnostics.unexpectedReplies++;
    }
    else if (validCRC)
        lapDiagnostics.unexpectedReplies++;

    lapExpectedLength = 0;
    lapPhase = 0;
    return true;
}

static void updateLapMotorRS485(SystemState &S)
{
    bool forward = S.motorDir == 1;
    bool reverse = S.motorDir == 3;
    bool running = S.RPMSetpoint > 0 && (forward || reverse);
    uint8_t wantedControl = running
        ? (forward ? CONTROL_FORWARD : CONTROL_REVERSE)
        : CONTROL_BRAKE;

    S.motorOn = running ? 1 : 0;

    if (lapExpectedLength)
    {
        collectLapReply(S);
        return;
    }

    if (millis() - lapLastFrameMs < 5)
        return;

    // An explicit diagnostic read takes priority over re-sending a failed
    // setpoint write, so a disconnected or misconfigured controller still
    // produces a useful probe result.
    if (lapProbeRequested)
    {
        lapProbeRequested = false;
        lapPhase = 3;
        readLapActualSpeed();
        return;
    }

    // The controller manual's speed examples use little-byte value order.
    if (lapSentRpm != S.RPMSetpoint)
    {
        uint16_t rpm = uint16_t(S.RPMSetpoint);
        lapPhase = 1;
        writeLapRegister(REG_COMMAND_RPM, uint8_t(rpm), uint8_t(rpm >> 8));
        lapSentRpm = S.RPMSetpoint;
        return;
    }

    if (lapSentControl != wantedControl)
    {
        lapPhase = 2;
        writeLapRegister(REG_CONTROL, wantedControl, LAP_MOTOR_POLE_PAIRS);
        lapSentControl = wantedControl;
        return;
    }

    if (millis() - lapLastFrameMs >= 250)
    {
        lapPhase = 3;
        readLapActualSpeed();
    }
}
#endif

void setTwistDriverEnabled(bool enabled)
{
    if (twistDriverIsEnabled == enabled)
        return;

    twistDriverIsEnabled = enabled;

    if (enabled)
        twistDriver.enable();
    else
        twistDriver.disable();
}

void setZedDriverEnabled(bool enabled)
{
    if (zedDriverIsEnabled == enabled)
        return;

    zedDriverIsEnabled = enabled;

    if (enabled)
        zedDriver.enable();
    else
        zedDriver.disable();
}

float shortestArcPath(float target, float current, float wheelIndex)
{
    if (!isfinite(target) || !isfinite(current) ||
        !isfinite(wheelIndex) || wheelIndex <= 0.0f)
    {
        return 0.0f;
    }

    float d = target - current;

    if (d > 0.0f)
    {
        if (d > wheelIndex * 0.5f)
            d -= wheelIndex;
    }
    else
    {
        if (-d > wheelIndex * 0.5f)
            d += wheelIndex;
    }

    return d;
}

bool twistMotionActive()
{
    return twistMoving || twistDirStep.distanceToGo() != 0;
}

static void stopTwistKeepLock()
{
    twistDirStep.stop();
    twistDirStep.move(0);
    twistDirStep.setCurrentPosition(0);
    twistDirStep.setSpeed(0.0f);

    digitalWrite(TWIST_STEP_PIN, LOW);

    twistMoving = false;
}

void hardStopTwist(SystemState &S)
{
    S.indexSpinRpm = 0.0f;
    S.twistLock = 0;
    S.twistReady = false;
    S.twistError = 0.0f;

    nLocks = 0;
    twistSettled = false;
    lastTwistTarget = NAN;
    lastTwistControlMs = 0;

    stopTwistKeepLock();
    setTwistDriverEnabled(false);
}

void cancelTwistMoveKeepLock(SystemState &S)
{
    S.twistError = 0.0f;
    nLocks = 0;
    twistSettled = false;
    lastTwistTarget = NAN;
    lastTwistControlMs = 0;

    stopTwistKeepLock();

    if (S.twistLock)
        setTwistDriverEnabled(true);
}

void hardStopZ()
{
    zedDirStep.stop();
    zedDirStep.move(0);
    zedDirStep.setCurrentPosition(0);
    zedDirStep.setSpeed(0.0f);

    digitalWrite(ZED_STEP_PIN, LOW);

    zMoving = false;
    setZedDriverEnabled(false);
}

void notifyTwistTargetChanged()
{
    nLocks = 0;
    twistSettled = false;
    lastTwistTarget = NAN;
    lastTwistControlMs = 0;
    stopTwistKeepLock();
}

bool requestZMove(long steps)
{
    if (steps == 0)
        return false;

    setZedDriverEnabled(true);
    zedDirStep.move(steps);
    zMoving = true;
    return true;
}

static void setMotorPins(SystemState &S)
{
#if USE_LAP_MOTOR_RS485
    updateLapMotorRS485(S);
#else
    if (S.motorDir == lastEscDir && S.RPMSetpoint == lastEscRpm)
        return;

    lastEscDir = S.motorDir;
    lastEscRpm = S.RPMSetpoint;

    bool en = false;
    bool fr = false;

    switch (S.motorDir)
    {
        case 0: en = false; fr = false; break;
        case 1: en = true;  fr = true;  break;
        case 2: en = false; fr = true;  break;
        case 3: en = true;  fr = false; break;
        default:
            S.motorDir = 1;
            en = true;
            fr = true;
            break;
    }

    digitalWrite(motorENpin, en);
    digitalWrite(motorFRpin, fr);
    digitalWrite(motorBKpin, LOW);

    S.motorOn = en ? 1 : 0;

    analogWrite(motorSVpin, S.RPMSetpoint);
#endif
}

void initSteppers()
{
    pinMode(TWIST_STEP_PIN, OUTPUT);
    pinMode(TWIST_DIR_PIN,  OUTPUT);
    digitalWrite(TWIST_STEP_PIN, LOW);
    digitalWrite(TWIST_DIR_PIN, LOW);

    pinMode(ZED_STEP_PIN, OUTPUT);
    pinMode(ZED_DIR_PIN,  OUTPUT);
    digitalWrite(ZED_STEP_PIN, LOW);
    digitalWrite(ZED_DIR_PIN, LOW);

    twistDriver.setup(twistSerial, TMC2209_BAUD);
    twistDriver.setHardwareEnablePin(TWIST_EN_PIN);
    twistDriver.setMicrostepsPerStep(TWIST_MICROSTEPS);
    twistDriver.setRMSCurrent(TWIST_RMS_CURRENT, 0.11f);
    twistDriver.enableAutomaticCurrentScaling();
    twistDriver.enableCoolStep();
    twistDriver.setStandstillMode(TMC2209::NORMAL);
    twistDriver.moveAtVelocity(0);
    twistDriver.moveUsingStepDirInterface();
    twistDriver.disable();
    twistDriverIsEnabled = false;

    zedDriver.setup(zedSerial, TMC2209_BAUD);
    zedDriver.setHardwareEnablePin(ZED_EN_PIN);
    zedDriver.setMicrostepsPerStep(ZED_MICROSTEPS);
    zedDriver.setRMSCurrent(ZED_RMS_CURRENT, 0.11f);
    zedDriver.enableAutomaticCurrentScaling();
    zedDriver.enableCoolStep();
    zedDriver.setStandstillMode(TMC2209::NORMAL);
    zedDriver.moveAtVelocity(0);
    zedDriver.moveUsingStepDirInterface();
    zedDriver.disable();
    zedDriverIsEnabled = false;

    pumpDriver.setup(pumpSerial, TMC2209_BAUD);
    pumpDriver.setHardwareEnablePin(PUMP_EN_PIN);
    pumpDriver.setMicrostepsPerStep(PUMP_MICROSTEPS);
    pumpDriver.setRMSCurrent(PUMP_RMS_CURRENT, 0.11f);
    pumpDriver.enableAutomaticCurrentScaling();
    pumpDriver.enableCoolStep();
    pumpDriver.enableInverseMotorDirection();
    pumpDriver.moveAtVelocity(0);
    pumpDriver.disable();

    twistDirStep.setMinPulseWidth(1);
    twistDirStep.setMaxSpeed(TWIST_MAX_SPEED);
    twistDirStep.setAcceleration(TWIST_ACCEL);
    twistDirStep.setSpeed(TWIST_STEP_SPEED);
    twistDirStep.setCurrentPosition(0);
    twistDirStep.move(0);

    zedDirStep.setMinPulseWidth(1);
    zedDirStep.setMaxSpeed(ZED_MAX_SPEED);
    zedDirStep.setAcceleration(ZED_ACCEL);
    zedDirStep.setSpeed(ZED_STEP_SPEED);
    zedDirStep.setCurrentPosition(0);
    zedDirStep.move(0);
}

void initESCMotor()
{
#if USE_LAP_MOTOR_RS485
    lapDiagnostics = LapMotorDiagnostics{};
    lapDiagnostics.enabled = true;
    lapMotorSerial.begin(LAP_MOTOR_BAUD);
#else
    pinMode(motorALMpin, OUTPUT);
    digitalWrite(motorALMpin, HIGH);

    pinMode(motorPGpin, INPUT);
    pinMode(motorSVpin, OUTPUT);
    pinMode(motorBKpin, OUTPUT);
    pinMode(motorENpin, OUTPUT);
    pinMode(motorFRpin, OUTPUT);

    analogWrite(motorSVpin, 0);
    digitalWrite(motorENpin, LOW);
    digitalWrite(motorFRpin, LOW);
    digitalWrite(motorBKpin, LOW);
#endif
}

const LapMotorDiagnostics& lapMotorDiagnostics()
{
    lapDiagnostics.awaitingReply =
#if USE_LAP_MOTOR_RS485
        lapExpectedLength != 0;
#else
        false;
#endif
    return lapDiagnostics;
}

void requestLapMotorProbe()
{
#if USE_LAP_MOTOR_RS485
    lapProbeRequested = true;
#endif
}

bool lapMotorLinkUp()
{
#if USE_LAP_MOTOR_RS485
    return lapDiagnostics.lastValidReplyMs != 0 &&
           millis() - lapDiagnostics.lastValidReplyMs < 1500;
#else
    return false;
#endif
}

static void updateTwistMotor(SystemState &S)
{
    if (fabsf(S.indexSpinRpm) > 0.0001f)
    {
        if (millis() - S.indexSpinLastCommandMs > 1500)
        {
            hardStopTwist(S);
            return;
        }
        S.twistLock = 0;
        S.twistError = 0.0f;
        nLocks = 0;
        twistSettled = false;
        lastTwistTarget = NAN;

        float stepsPerSecond = S.indexSpinRpm * S.wheelIndex *
                               tiltStepsPerIndexUnit / 60.0f;
        stepsPerSecond *= TWIST_MOTOR_SIGN * S.indexSign;
        stepsPerSecond = constrain(stepsPerSecond, -TWIST_MAX_SPEED, TWIST_MAX_SPEED);

        setTwistDriverEnabled(true);
        twistDirStep.setMaxSpeed(TWIST_MAX_SPEED);
        twistDirStep.setSpeed(stepsPerSecond);
        twistDirStep.runSpeed();
        twistMoving = true;
        return;
    }

    if (!S.twistLock || !S.twistReady || !S.targetValid ||
        S.wheelIndex <= 0.0f || tiltStepsPerIndexUnit <= 0.0f)
    {
        S.twistError = 0.0f;
        nLocks = 0;
        twistSettled = false;
        stopTwistKeepLock();

        if (!S.twistLock)
            setTwistDriverEnabled(false);

        return;
    }

    if (!isfinite(lastTwistTarget) ||
        fabsf(shortestArcPath(S.targetTwist, lastTwistTarget, S.wheelIndex)) > 0.0001f)
    {
        lastTwistTarget = S.targetTwist;
        nLocks = 0;
        twistSettled = false;
        stopTwistKeepLock();
    }

    setTwistDriverEnabled(true);

    if (twistDirStep.distanceToGo() != 0)
    {
        twistMoving = true;
        twistDirStep.run();
    }
    else
    {
        twistMoving = false;
    }

    uint32_t now = millis();

    if ((now - lastTwistControlMs) <= TWIST_CONTROL_INTERVAL_MS)
    {
        if (twistDirStep.distanceToGo() != 0)
            twistDirStep.run();

        return;
    }

    lastTwistControlMs = now;

    if (nLocks > TWIST_SETTLE_FRAMES)
    {
        twistSettled = true;
        twistDirStep.move(0);
        setTwistDriverEnabled(true);
        return;
    }

    float err = shortestArcPath(S.targetTwist, S.actualTwist, S.wheelIndex);
    float absErr = fabsf(err);

    if (!isfinite(err) || absErr > (S.wheelIndex * 0.5f))
    {
        hardStopTwist(S);
        return;
    }

    S.twistError = err;

    if (absErr > POSITION_ERROR_TOL)
    {
        nLocks = 0;
        twistSettled = false;

        long totalSteps = lroundf(err * tiltStepsPerIndexUnit) *
                          TWIST_MOTOR_SIGN * S.indexSign;
        long steps = lroundf(float(totalSteps) * TWIST_CORRECTION_GAIN);

        if (steps == 0 && totalSteps != 0)
            steps = (totalSteps > 0) ? 1 : -1;

        twistDirStep.setMaxSpeed(TWIST_MAX_SPEED);
        twistDirStep.setAcceleration(TWIST_ACCEL);

        if (steps != 0 && twistDirStep.distanceToGo() == 0)
            twistDirStep.move(steps);

        if (twistDirStep.distanceToGo() != 0)
            twistDirStep.run();

        return;
    }

    nLocks++;
}

static void updateZMotor(SystemState &S)
{
    if (!S.zLock)
    {
        hardStopZ();
        return;
    }

    setZedDriverEnabled(true);

    if (zedDirStep.distanceToGo() != 0)
    {
        zMoving = true;
        zedDirStep.run();
        return;
    }

    if (zMoving)
    {
        zMoving = false;
        zedDirStep.move(0);
        digitalWrite(ZED_STEP_PIN, LOW);
        return;
    }

    digitalWrite(ZED_STEP_PIN, LOW);
}

static void updatePump(SystemState &S)
{
    int flow = int(S.flowSetpoint);

    if (S.flow_dir == lastPumpDir && flow == lastPumpFlow)
        return;

    lastPumpDir = S.flow_dir;
    lastPumpFlow = flow;

    switch (S.flow_dir)
    {
        case 0:
            pumpDriver.enable();
            pumpDriver.enableInverseMotorDirection();
            pumpDriver.moveAtVelocity(int32_t(flow) * 4);
            break;

        case 1:
            pumpDriver.disable();
            pumpDriver.enableInverseMotorDirection();
            pumpDriver.moveAtVelocity(0);
            break;

        case 2:
            pumpDriver.enable();
            pumpDriver.disableInverseMotorDirection();
            pumpDriver.moveAtVelocity(int32_t(flow) * 4);
            break;

        case 3:
            pumpDriver.disable();
            pumpDriver.disableInverseMotorDirection();
            pumpDriver.moveAtVelocity(0);
            break;

        default:
            S.flow_dir = 1;
            lastPumpDir = S.flow_dir;
            pumpDriver.disable();
            pumpDriver.moveAtVelocity(0);
            break;
    }
}

void updateMotors(SystemState &S)
{
    updateTwistMotor(S);
    updateZMotor(S);
    updatePump(S);
    setMotorPins(S);
}

void updateWheelIndex(SystemState &S, float newWheelValue)
{
    if (!isfinite(newWheelValue) || newWheelValue <= 0.0f)
        newWheelValue = 96.0f;

    S.wheelIndex = newWheelValue;

    // Current mast sends twist 0..4095.
    tiltConversion = newWheelValue / 4096.0f;

    tiltStepsPerIndexUnit =
        float(TWIST_MOTOR_STEPS_PER_ROT * TWIST_MICROSTEPS) / newWheelValue;
}
