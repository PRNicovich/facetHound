#include "keyboardFunctions.h"
#include "motorControl.h"
#include <Arduino.h>
#include <math.h>

static const float supportedIndexes[] = {
    1.0f, 2.0f, 6.2832f, 32.0f, 40.0f, 48.0f, 60.0f, 64.0f,
    72.0f, 77.0f, 80.0f, 81.0f, 88.0f, 91.0f, 96.0f, 98.0f,
    99.0f, 100.0f, 102.0f, 104.0f, 110.0f, 120.0f, 128.0f,
    144.0f, 192.0f, 256.0f, 360.0f, 400.0f
};

static const int N_INDEXES = 28;
static const float tiltMult[3] = { 1.0f, 0.1f, 0.005f };

static const float defaultMarkPoints[] = {
    0, 6, 12, 18, 24, 30, 36, 42,
    48, 54, 60, 66, 72, 78, 84, 90
};

static const int N_DEFAULT_MARKS =
    sizeof(defaultMarkPoints) / sizeof(defaultMarkPoints[0]);

static const float servoRelease = 120.0f;
static const float servoCapture = 105.0f;
static bool firstCrossServoThreshold = true;

static uint32_t lastMotorDirClick = 0;
static bool     inClickTimer_ESC  = false;
static uint32_t lastFlowDirClick  = 0;
static bool     inClickTimer_pump = false;

static const uint32_t ESC_CLICK_WINDOW_MS = 2 * DOUBLE_CLICK_MS;

static int nearestWheelIndex(float wheel)
{
    int best = 0;
    float bestErr = fabsf(wheel - supportedIndexes[0]);

    for (int i = 1; i < N_INDEXES; i++)
    {
        float e = fabsf(wheel - supportedIndexes[i]);

        if (e < bestErr)
        {
            bestErr = e;
            best = i;
        }
    }

    return best;
}

static void wrapTarget(SystemState* S)
{
    if (S->wheelIndex <= 0.0f)
        S->wheelIndex = 96.0f;

    while (S->targetTwist < 0.0f)
        S->targetTwist += S->wheelIndex;

    while (S->targetTwist >= S->wheelIndex)
        S->targetTwist -= S->wheelIndex;
}

void resetMarkPointsToDefault(SystemState* S)
{
    S->markPoints.assign(defaultMarkPoints, defaultMarkPoints + N_DEFAULT_MARKS);

    S->markIdx = 0;

    if (!S->markPoints.empty())
    {
        S->targetTwist = S->markPoints[0];
        S->targetValid = true;
        notifyTwistTargetChanged();
    }

    S->dirty = true;
}

void resetMarkPointsWithSpacing(SystemState* S, float spacing)
{
    if (!isfinite(spacing) || spacing <= 0.0f ||
        !isfinite(S->wheelIndex) || S->wheelIndex <= 0.0f)
        return;

    const size_t count = size_t(ceilf(S->wheelIndex / spacing));
    S->markPoints.clear();
    S->markPoints.reserve(count);

    for (float value = 0.0f; value < S->wheelIndex - 0.0001f; value += spacing)
        S->markPoints.push_back(value);

    S->markIdx = 0;
    if (!S->markPoints.empty())
    {
        S->targetTwist = S->markPoints.front();
        S->targetValid = true;
        notifyTwistTargetChanged();
    }

    S->dirty = true;
}

void checkServoStatus(SystemState* S)
{
    if (!S->spinServoIdx)
        return;

    if (S->tipDegrees > servoRelease)
    {
        if (firstCrossServoThreshold)
        {
            S->twistLock = 0;
            firstCrossServoThreshold = false;
            hardStopTwist(*S);
        }
    }
    else if (S->tipDegrees < servoCapture)
    {
        if (!firstCrossServoThreshold)
        {
            S->twistLock = 1;
            firstCrossServoThreshold = true;
            notifyTwistTargetChanged();
            twistDriver.enable();
        }
    }
}

void setSpinServoEnabled(SystemState* S, bool enabled)
{
    S->spinServoIdx = enabled ? 1 : 0;
    firstCrossServoThreshold = true;
    S->dirty = true;
}

void changeTiltAngle(SystemState* S, bool directSet)
{
    if (!S->targetValid)
    {
        if (S->twistReady)
            S->targetTwist = S->actualTwist;
        else if (!S->markPoints.empty())
            S->targetTwist = S->markPoints[S->markIdx];
        else
            S->targetTwist = 0.0f;

        S->targetValid = true;
    }

    if (!directSet)
    {
        float inc = tiltMult[S->twistIdx];

        if (S->twistDir)
            S->targetTwist += inc;
        else
            S->targetTwist -= inc;
    }

    wrapTarget(S);
    notifyTwistTargetChanged();

    if (S->twistLock)
        twistDriver.enable();

    S->dirty = true;
}

void toggleTiltLock(SystemState* S)
{
    S->twistLock = !S->twistLock;

    notifyTwistTargetChanged();

    if (S->twistLock)
    {
        if (!S->targetValid)
        {
            if (S->twistReady)
                S->targetTwist = S->actualTwist;
            else if (!S->markPoints.empty())
                S->targetTwist = S->markPoints[S->markIdx];
            else
                S->targetTwist = 0.0f;

            S->targetValid = true;
        }

        twistDriver.enable();
    }
    else
    {
        hardStopTwist(*S);
    }

    S->dirty = true;
}

void changeTwistIndex(SystemState* S)
{
    S->twistIdx = (S->twistIdx + 1) % 3;
    S->tiltMicrostep = tiltMult[S->twistIdx];
    S->dirty = true;
}

void changeFlowRate(SystemState* S, int delta)
{
    S->flowSetpoint += delta;

    if (S->flowSetpoint < 0)
        S->flowSetpoint = 0;

    if (S->flowSetpoint > 750)
        S->flowSetpoint = 750;

    S->dirty = true;
}

void changeFlowDirection(SystemState* S, bool isDoubleClick)
{
    if (isDoubleClick)
    {
        if      (S->flow_dir == 0) S->flow_dir = 3;
        else if (S->flow_dir == 1) S->flow_dir = 3;
        else if (S->flow_dir == 2) S->flow_dir = 1;
        else                       S->flow_dir = 1;
    }
    else
    {
        if      (S->flow_dir == 0) S->flow_dir = 1;
        else if (S->flow_dir == 1) S->flow_dir = 0;
        else if (S->flow_dir == 2) S->flow_dir = 3;
        else                       S->flow_dir = 2;
    }

    S->dirty = true;
}

void toggleFlowPause(SystemState* S)
{
    S->flowPaused = !S->flowPaused;
    S->dirty = true;
}

void changeMotorSpeed(SystemState* S, int delta)
{
    S->RPMSetpoint += delta;

    if (S->RPMSetpoint < RPM_LIMIT_LOW)
        S->RPMSetpoint = RPM_LIMIT_LOW;

    if (S->RPMSetpoint > RPM_LIMIT_HIGH)
        S->RPMSetpoint = RPM_LIMIT_HIGH;

    S->dirty = true;
}

void changeMotorDirection(SystemState* S, bool isDoubleClick)
{
    if (isDoubleClick)
    {
        if (S->motorDir == 0 || S->motorDir == 3)
            S->motorDir = 1;
        else
            S->motorDir = 3;
    }
    else
    {
        if      (S->motorDir == 0) S->motorDir = 3;
        else if (S->motorDir == 3) S->motorDir = 0;
        else if (S->motorDir == 1) S->motorDir = 2;
        else                       S->motorDir = 1;
    }

    S->dirty = true;
}

void toggleMotorPause(SystemState* S)
{
    S->motorPaused = !S->motorPaused;
    S->dirty = true;
}

void toggleZLock(SystemState* S)
{
    S->zLock = !S->zLock;

    if (!S->zLock)
        hardStopZ();

    S->dirty = true;
}

void changeZMotorSteps(SystemState* S, bool dirDown)
{
    if (!S->zLock)
        return;

    long move = (dirDown ? zMult[S->zIdx] : -zMult[S->zIdx]) * S->zSign;
    requestZMove(move);
}

void changeZMultiplier(SystemState* S)
{
    S->zIdx = (S->zIdx + 1) % 3;
    S->dirty = true;
}

void addPositionToList(SystemState* S, float pos)
{
    S->markPoints.push_back(pos);
    S->dirty = true;
}

void updatePositionInList(SystemState* S, int idx, float pos)
{
    if (idx < 0 || size_t(idx) >= S->markPoints.size())
        return;

    S->markPoints[idx] = pos;
    S->dirty = true;
}

void deletePositionInList(SystemState* S, int idx)
{
    if (idx < 0 || size_t(idx) >= S->markPoints.size())
        return;

    S->markPoints.erase(S->markPoints.begin() + idx);

    if (size_t(S->markIdx) >= S->markPoints.size())
        S->markIdx = S->markPoints.empty() ? 0 : int(S->markPoints.size()) - 1;

    homeMarkPoint(S);
    S->dirty = true;
}

void changeMarkPointIndex(SystemState* S, bool forward)
{
    if (S->markPoints.empty())
        return;

    if (forward)
        S->markIdx++;
    else
        S->markIdx--;

    if (S->markIdx < 0)
        S->markIdx = int(S->markPoints.size()) - 1;
    else if (size_t(S->markIdx) >= S->markPoints.size())
        S->markIdx = 0;

    homeMarkPoint(S);
    S->dirty = true;
}

void homeMarkPoint(SystemState* S)
{
    if (S->markPoints.empty())
        return;

    S->targetTwist = S->markPoints[S->markIdx];
    S->targetValid = true;

    wrapTarget(S);
    notifyTwistTargetChanged();

    S->dirty = true;
}

void toggleSpinServo(SystemState* S)
{
    setSpinServoEnabled(S, !S->spinServoIdx);
}

void toggleCheatMode(SystemState* S)
{
    if (S->markPoints.empty())
        return;

    float setPoint = S->markPoints[S->markIdx];
    float delta = S->targetTwist - setPoint;

    for (size_t i = 0; i < S->markPoints.size(); i++)
    {
        float nm = S->markPoints[i] + delta;

        while (nm >= S->wheelIndex)
            nm -= S->wheelIndex;

        while (nm < 0.0f)
            nm += S->wheelIndex;

        S->markPoints[i] = nm;
    }

    homeMarkPoint(S);
    S->dirty = true;
}

void incrementWheelIndex(SystemState* S)
{
    int idx = nearestWheelIndex(S->wheelIndex);
    idx = (idx + 1) % N_INDEXES;

    updateWheelIndex(*S, supportedIndexes[idx]);
    resetMarkPointsToDefault(S);
    homeMarkPoint(S);

    S->dirty = true;
}

void pollDoubleClick(SystemState* S)
{
    uint32_t now = millis();

    if (inClickTimer_ESC && (now - lastMotorDirClick) > 2 * DOUBLE_CLICK_MS)
    {
        inClickTimer_ESC = false;
        changeMotorDirection(S, false);
    }

    if (inClickTimer_pump && (now - lastFlowDirClick) > 2 * DOUBLE_CLICK_MS)
    {
        inClickTimer_pump = false;
        changeFlowDirection(S, false);
    }
}

void handleKey(SystemState* S, uint8_t key)
{
    switch (key)
    {
        case 6:
            toggleCheatMode(S);
            break;

        case 7:
            toggleZLock(S);
            break;

        case 8:
            addPositionToList(S, S->targetTwist);
            break;

        case 9:
            updatePositionInList(S, S->markIdx, S->targetTwist);
            break;

        case 10:
            deletePositionInList(S, S->markIdx);
            break;

        case 12:
            changeMarkPointIndex(S, true);
            break;

        case 13:
            changeMarkPointIndex(S, false);
            break;

        case 14:
            homeMarkPoint(S);
            break;

        case 15:
            toggleTiltLock(S);
            break;

        case 16:
            S->twistDir = 1;
            changeTiltAngle(S, false);
            break;

        case 17:
            changeTwistIndex(S);
            break;

        case 18:
            S->twistDir = 0;
            changeTiltAngle(S, false);
            break;

        case 30:
            changeMotorSpeed(S, -1);
            break;

        case 31:
        {
            uint32_t now = millis();

            if (inClickTimer_ESC && (now - lastMotorDirClick) <= ESC_CLICK_WINDOW_MS)
            {
                changeMotorDirection(S, true);
                inClickTimer_ESC = false;
            }
            else
            {
                inClickTimer_ESC = true;
            }

            lastMotorDirClick = now;
            break;
        }

        case 32:
            changeMotorSpeed(S, +1);
            break;

        case 33:
            changeFlowRate(S, -1);
            break;

        case 34:
        {
            uint32_t now = millis();

            if ((now - lastFlowDirClick) < DOUBLE_CLICK_MS)
            {
                changeFlowDirection(S, true);
                inClickTimer_pump = false;
            }
            else
            {
                inClickTimer_pump = true;
            }

            lastFlowDirClick = now;
            break;
        }

        case 35:
            changeFlowRate(S, +1);
            break;

        case 36:
            changeZMotorSteps(S, true);
            break;

        case 37:
            changeZMultiplier(S);
            break;

        case 38:
            changeZMotorSteps(S, false);
            break;

        default:
            break;
    }
}
