#pragma once

#include <Arduino.h>
#include "systemState.h"

void handleKey(SystemState* S, uint8_t k);
void pollDoubleClick(SystemState* S);

void changeTiltAngle(SystemState* S, bool directSet);
void toggleTiltLock(SystemState* S);
void changeTwistIndex(SystemState* S);

void changeFlowRate(SystemState* S, int delta);
void changeFlowDirection(SystemState* S, bool isDoubleClick);
void toggleFlowPause(SystemState* S);

void changeMotorSpeed(SystemState* S, int delta);
void changeMotorDirection(SystemState* S, bool isDoubleClick);
void toggleMotorPause(SystemState* S);

void toggleZLock(SystemState* S);
void changeZMotorSteps(SystemState* S, bool dirDown);
void changeZMultiplier(SystemState* S);

void addPositionToList(SystemState* S, float pos);
void updatePositionInList(SystemState* S, int idx, float pos);
void deletePositionInList(SystemState* S, int idx);
void changeMarkPointIndex(SystemState* S, bool forward);
void homeMarkPoint(SystemState* S);

void resetMarkPointsToDefault(SystemState* S);
void resetMarkPointsWithSpacing(SystemState* S, float spacing);
void checkServoStatus(SystemState* S);
void setSpinServoEnabled(SystemState* S, bool enabled);

void toggleSpinServo(SystemState* S);
void toggleCheatMode(SystemState* S);
void incrementWheelIndex(SystemState* S);
