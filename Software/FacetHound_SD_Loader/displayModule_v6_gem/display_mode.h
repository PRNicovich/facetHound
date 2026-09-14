#pragma once

#include <Arduino.h>

enum class DisplayMode : uint8_t
{
    CLASSIC = 0,
    DYNAMIC = 1,
    STATIC = 2,
};

inline const char* displayModeName(DisplayMode mode)
{
    switch (mode)
    {
        case DisplayMode::CLASSIC: return "CLASSIC";
        case DisplayMode::DYNAMIC: return "DYNAMIC";
        case DisplayMode::STATIC:  return "STATIC";
    }

    return "CLASSIC";
}
