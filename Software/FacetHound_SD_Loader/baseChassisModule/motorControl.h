#pragma once

#include <Arduino.h>
#include <AccelStepper.h>
#include <TMC2209.h>
#include <SerialPIO.h>
#include "systemState.h"

// ================= PINS =================

// Twist stepper
#define TWIST_EN_PIN    14
#define TWIST_TX_PIN    28
#define TWIST_DIR_PIN    0
#define TWIST_STEP_PIN   1

// Z stepper
#define ZED_EN_PIN      18
#define ZED_TX_PIN      15
#define ZED_DIR_PIN     17
#define ZED_STEP_PIN    16

// Pump
#define PUMP_EN_PIN     22
#define PUMP_TX_PIN     19
#define PUMP_DIR_PIN    21
#define PUMP_STEP_PIN   20

// Lap motor communication. Set to 0 to restore the legacy EN/FR/BK/SV wiring.
#define USE_LAP_MOTOR_RS485  1

// BLD-510B Modbus RTU / RS-485 through the v9 PCB's ESC.TTL header and an
// external automatic-direction 3.3 V TTL-to-RS-485 module.
#define LAP_MOTOR_TX_PIN     8
#define LAP_MOTOR_RX_PIN     9
#define LAP_MOTOR_BAUD    9600
#define LAP_MOTOR_SLAVE_ID   1
#define LAP_MOTOR_POLE_PAIRS 2

// Legacy lap-motor pins
#define motorALMpin      8
#define motorPGpin       9
#define motorSVpin      10
#define motorBKpin      11
#define motorENpin      12
#define motorFRpin      13

// ================= DRIVER SETTINGS =================

#define TMC2209_BAUD 19200

#define TWIST_RMS_CURRENT   1000
#define TWIST_MICROSTEPS     256
// These are STEP pulse rates, not index units.  The previous 200 kHz ceiling
// was unsafe when a TMC2208 came up at its strap-selected microstep setting.
// The former mid-speed is now the absolute ceiling.  This acceleration makes
// a ten-index-unit correction just reach that ceiling before slowing down.
#define TWIST_MAX_SPEED     1800.0f
#define TWIST_ACCEL          650.0f
#define TWIST_STEP_SPEED    2500.0f

#define ZED_RMS_CURRENT      800
#define ZED_MICROSTEPS       128
// Z jogs are deliberately conservative during integration.  AccelStepper's
// run() uses MAX_SPEED and ACCEL; STEP_SPEED is retained for compatibility.
#define ZED_MAX_SPEED       2500.0f
#define ZED_STEP_SPEED      2500.0f
#define ZED_ACCEL           4000.0f
#define PUMP_RMS_CURRENT    1100
#define PUMP_MICROSTEPS        4

#define TWIST_MOTOR_STEPS_PER_ROT  200
#define ZED_MOTOR_STEPS_PER_ROT    100

// Classic Z constants.
#define ZED_INDEX_INITS_PER_TURN  5e-3f
#define ZED_GEAR_RATIO            4.0f
#define ZED_ENC_OVERSAMPLE        4.0f

// Twist correction.
#define POSITION_ERROR_TOL      0.013f
#define TWIST_SETTLE_FRAMES     3
#define TWIST_CONTROL_INTERVAL_MS 5
#define TWIST_CORRECTION_GAIN   1.0f
#define TWIST_CORRECTION_LIMIT 10.0f

// Change to -1 only if twist moves away from target.
#define TWIST_MOTOR_SIGN        1

// Z step sizing.
// 20,000 pulses/mm gives:
// zIdx 0 = 0.100 mm = 2000 pulses
// zIdx 1 = 0.010 mm = 200 pulses
// zIdx 2 = 0.001 mm = 20 pulses
#define Z_STEPS_PER_MM          20000.0f

#define RPM_LIMIT_LOW     0
#define RPM_LIMIT_HIGH  200

#define DOUBLE_CLICK_MS 150

extern AccelStepper twistDirStep;
extern AccelStepper zedDirStep;

extern TMC2209 twistDriver;
extern TMC2209 zedDriver;
extern TMC2209 pumpDriver;

extern float tiltStepsPerIndexUnit;
extern float tiltConversion;
extern float zConversionFactor;
extern int   zMult[3];

struct LapMotorDiagnostics
{
    bool enabled = false;
    bool awaitingReply = false;
    uint32_t txFrames = 0;
    uint32_t rxBytes = 0;
    uint32_t validReplies = 0;
    uint32_t writeAcks = 0;
    uint32_t readReplies = 0;
    uint32_t timeouts = 0;
    uint32_t crcErrors = 0;
    uint32_t exceptions = 0;
    uint32_t unexpectedReplies = 0;
    uint32_t lastValidReplyMs = 0;
    uint32_t lastRxByteMs = 0;
    uint8_t lastFunction = 0;
    uint8_t lastException = 0;
    uint8_t lastReply[8] = {};
    uint8_t lastReplyLength = 0;
};

void initSteppers();
void initESCMotor();
void updateMotors(SystemState &S);
void updateWheelIndex(SystemState &S, float newWheelValue);
void setTwistDriverEnabled(bool enabled);
void setZedDriverEnabled(bool enabled);

float shortestArcPath(float target, float current, float wheelIndex);

void notifyTwistTargetChanged();
void hardStopTwist(SystemState &S);
void cancelTwistMoveKeepLock(SystemState &S);
void hardStopZ();
bool requestZMove(long steps);
bool twistMotionActive();
const LapMotorDiagnostics& lapMotorDiagnostics();
void requestLapMotorProbe();
bool lapMotorLinkUp();
