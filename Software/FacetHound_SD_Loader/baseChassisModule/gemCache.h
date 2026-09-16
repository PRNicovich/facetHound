#pragma once

#include <Arduino.h>

#include "gemGeometry.h"
#include "sdGemLoader.h"

enum class GemCacheResult : uint8_t
{
    OK,
    NOT_FOUND,
    SOURCE_OPEN_FAILED,
    CACHE_OPEN_FAILED,
    STALE,
    BAD_HEADER,
    BAD_COUNTS,
    SHORT_READ,
    WRITE_FAILED,
};

GemCacheResult loadGemCache(const char* sourcePath, GemSdDesign* design,
                            GemRuntimeGeometry* geometry);
GemCacheResult saveGemCache(const char* sourcePath, const GemSdDesign& design,
                            const GemRuntimeGeometry& geometry);
const char* gemCacheResultText(GemCacheResult result);
