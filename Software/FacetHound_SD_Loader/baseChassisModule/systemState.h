#pragma once
#include <Arduino.h>
#include <vector>

struct SystemState
{
    uint32_t mastRxCount = 0;

    // Twist / index
    float targetTwist  = 0.0f;
    float actualTwist  = 0.0f;
    float twistError   = 0.0f;
    int   twistDir     = 1;
    int   indexSign    = 1;        // +1 = CW-positive, -1 = CCW-positive
    int   twistEncoderRaw = 0;
    int   twistZeroRaw = 0;
    float indexSpinRpm = 0.0f;     // signed, open-loop continuous index rotation
    uint32_t indexSpinLastCommandMs = 0;
    int   twistLock    = 0;
    int   twistIdx     = 1;
    bool  twistReady   = false;
    bool  targetValid  = false;

    float wheelIndex    = 96.0f;
    float tiltMicrostep = 0.1f;

    // Mast sensors
    int   tipEncoder = 0;
    int   tipZeroRaw = 47850;
    float tipDegrees = 0.0f;
    bool  tableAdapter = false;    // subtract 45 degrees from calibrated tip

    // Flow / pump
    float flowSetpoint = 0.0f;
    float flowTicksToMlMin = 0.052f;
    int   flow_dir     = 1;
    int   flowPaused   = 0;

    // Z axis
    int   zIdx        = 0;
    int   zLock       = 0;
    int   zEncoderRaw = 0;
    int   zSign       = 1;
    float zMM         = 0.0f;

    // ESC spindle
    int RPMSetpoint = 0;
    int RPMValue    = 0;
    // 0 = paused CCW, 1 = running CW, 2 = paused CW, 3 = running CCW.
    int motorDir    = 1;
    int motorPaused = 0;
    int motorOn     = 0;

    // Force sensor
    float forceValue = 0.0f;

    // Mark points
    std::vector<float> markPoints;
    int   markIdx   = 0;

    // Spin servo
    int spinServoIdx = 0;

    // Cheat mode
    bool cheatMode = false;

    // Save debounce
    bool dirty = false;
};
