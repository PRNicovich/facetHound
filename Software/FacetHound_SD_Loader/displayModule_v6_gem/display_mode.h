#pragma once

#include <Arduino.h>

enum class DisplayMode : uint8_t
{
    CLASSIC = 0,
    DYNAMIC = 1,
    STATIC = 2,
    CLASSIC_TIER = 3,
};

inline const char* displayModeName(DisplayMode mode)
{
    switch (mode)
    {
        case DisplayMode::CLASSIC: return "CLASSIC";
        case DisplayMode::DYNAMIC: return "DYNAMIC";
        case DisplayMode::STATIC:  return "STATIC";
        case DisplayMode::CLASSIC_TIER: return "CLASSIC_TIER";
    }

    return "CLASSIC";
}

inline bool isClassicView(DisplayMode mode) {
    return mode == DisplayMode::CLASSIC || mode == DisplayMode::CLASSIC_TIER;
}
