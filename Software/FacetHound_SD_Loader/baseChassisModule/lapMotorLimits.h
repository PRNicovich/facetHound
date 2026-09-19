#pragma once

// Serial BLD commands are RPM, not the legacy 0..200 PWM range.
// Match the upper speed exercised by the original BLD demonstration sketch.
// This does not raise the saved setpoint or impose a minimum/startup boost.
#define RPM_LIMIT_LOW 0
#define RPM_LIMIT_HIGH 1500
