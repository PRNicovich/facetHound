#pragma once

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "systemState.h"

#if defined(ARDUINO_ARCH_RP2040) && __has_include(<hardware/flash.h>) && \
    __has_include(<hardware/sync.h>) && __has_include(<hardware/regs/addressmap.h>)
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <hardware/regs/addressmap.h>
#define STATE_HAS_RAW_FLASH 1
#endif

#ifndef STATE_HAS_RAW_FLASH
#define STATE_HAS_RAW_FLASH 0
#endif

static const char* STATE_FLASH_PATH = "last-64k";

static const uint32_t STATE_MAGIC = 0x46485744UL;
static const uint16_t STATE_VERSION = 4;

#if STATE_HAS_RAW_FLASH
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif
// Sixteen sectors provide room for 16,320 float positions. The runtime list is
// dynamically sized; this is only the persistence capacity of the reserved
// raw-flash region.
static const uint32_t STATE_FLASH_BYTES = 16 * FLASH_SECTOR_SIZE;
static const uint32_t STATE_FLASH_OFFSET = PICO_FLASH_SIZE_BYTES - STATE_FLASH_BYTES;
static const uint32_t STATE_HEADER_BYTES = FLASH_PAGE_SIZE;
static const uint32_t STATE_MAX_SAVED_POSITIONS =
    (STATE_FLASH_BYTES - STATE_HEADER_BYTES) / sizeof(float);
#endif

enum StateLoadResult
{
    STATE_LOAD_OK = 0,
    STATE_LOAD_NO_FILE,
    STATE_LOAD_OPEN_FAILED,
    STATE_LOAD_SHORT_READ,
    STATE_LOAD_BAD_HEADER,
    STATE_LOAD_BAD_CHECKSUM
};

static StateLoadResult lastStateLoadResult = STATE_LOAD_NO_FILE;
static size_t lastStateBytesWritten = 0;
static const char* lastStateSaveBackend = "none";
static const char* lastStateSavePath = "";
static const char* lastStateLoadBackend = "none";
static const char* lastStateLoadPath = "";

inline void setStateFilesystemAvailable(bool) {}

struct SavedStateV3Header
{
    uint32_t magic;
    uint16_t version;
    uint16_t headerBytes;
    uint32_t totalBytes;

    float targetTwist;
    float flowSetpoint;
    float wheelIndex;

    int32_t zIdx;
    int32_t twistIdx;
    int32_t flowDir;
    int32_t motorDir;
    int32_t rpmSetpoint;
    int32_t spinServoIdx;
    int32_t indexSign;
    int32_t tableAdapter;
    int32_t markIdx;
    uint32_t markCount;

    uint32_t checksum;
};

struct SavedStateV4Header
{
    uint32_t magic;
    uint16_t version;
    uint16_t headerBytes;
    uint32_t totalBytes;

    float targetTwist;
    float flowSetpoint;
    float wheelIndex;

    int32_t zIdx;
    int32_t twistIdx;
    int32_t flowDir;
    int32_t motorDir;
    int32_t rpmSetpoint;
    int32_t spinServoIdx;
    int32_t indexSign;
    int32_t tableAdapter;
    int32_t twistZeroRaw;
    int32_t tipZeroRaw;
    int32_t zSign;
    float flowTicksToMlMin;
    int32_t markIdx;
    uint32_t markCount;

    uint32_t checksum;
};

// Previous fixed-list format, retained only for one-time migration.
struct SavedStateV2Legacy
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    float targetTwist;
    float flowSetpoint;
    float wheelIndex;
    int32_t zIdx;
    int32_t twistIdx;
    int32_t flowDir;
    int32_t motorDir;
    int32_t rpmSetpoint;
    int32_t spinServoIdx;
    int32_t markCount;
    int32_t markIdx;
    float markPoints[16];
    uint32_t checksum;
};

static_assert(sizeof(SavedStateV3Header) <= FLASH_PAGE_SIZE,
              "state header must fit one flash page");
static_assert(sizeof(SavedStateV4Header) <= FLASH_PAGE_SIZE,
              "state header must fit one flash page");

static uint32_t stateChecksumUpdate(uint32_t checksum, const uint8_t* data, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        checksum ^= data[i];
        checksum *= 16777619UL;
    }
    return checksum;
}

template <typename Header>
static uint32_t stateChecksum(const Header& source, const float* positions, size_t count)
{
    Header header = source;
    header.checksum = 0;
    uint32_t checksum = stateChecksumUpdate(
        2166136261UL, reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    if (count > 0)
        checksum = stateChecksumUpdate(
            checksum, reinterpret_cast<const uint8_t*>(positions), count * sizeof(float));
    return checksum;
}

static uint32_t legacyChecksum(SavedStateV2Legacy source)
{
    source.checksum = 0;
    return stateChecksumUpdate(
        2166136261UL, reinterpret_cast<const uint8_t*>(&source), sizeof(source));
}

static void sanitizeState(SystemState& S);

#if STATE_HAS_RAW_FLASH
static bool loadLegacyStateAt(SystemState& S, const uint8_t* base)
{
    const SavedStateV2Legacy legacy =
        *reinterpret_cast<const SavedStateV2Legacy*>(base);
    if (legacy.magic != STATE_MAGIC || legacy.version != 2 ||
        legacy.size != sizeof(legacy) || legacy.checksum != legacyChecksum(legacy) ||
        legacy.markCount < 0 || legacy.markCount > 16)
        return false;

    S.targetTwist = legacy.targetTwist;
    S.flowSetpoint = legacy.flowSetpoint;
    S.wheelIndex = legacy.wheelIndex;
    S.zIdx = legacy.zIdx;
    S.twistIdx = legacy.twistIdx;
    S.flow_dir = legacy.flowDir;
    S.motorDir = legacy.motorDir;
    S.RPMSetpoint = legacy.rpmSetpoint;
    S.spinServoIdx = legacy.spinServoIdx;
    S.indexSign = 1;
    S.tableAdapter = false;
    S.markIdx = legacy.markIdx;
    S.markPoints.assign(legacy.markPoints, legacy.markPoints + legacy.markCount);
    sanitizeState(S);
    S.dirty = true; // autosave migrates the old record to v4
    lastStateLoadResult = STATE_LOAD_OK;
    return true;
}

static bool loadV3StateAt(SystemState& S, const uint8_t* base)
{
    const SavedStateV3Header header =
        *reinterpret_cast<const SavedStateV3Header*>(base);
    if (header.magic != STATE_MAGIC || header.version != 3 ||
        header.headerBytes != STATE_HEADER_BYTES ||
        header.markCount > STATE_MAX_SAVED_POSITIONS ||
        header.totalBytes != STATE_HEADER_BYTES + header.markCount * sizeof(float) ||
        header.totalBytes > STATE_FLASH_BYTES)
        return false;

    const float* positions = reinterpret_cast<const float*>(base + STATE_HEADER_BYTES);
    if (header.checksum != stateChecksum(header, positions, header.markCount))
        return false;

    S.targetTwist = header.targetTwist;
    S.flowSetpoint = header.flowSetpoint;
    S.wheelIndex = header.wheelIndex;
    S.zIdx = header.zIdx;
    S.twistIdx = header.twistIdx;
    S.flow_dir = header.flowDir;
    S.motorDir = header.motorDir;
    S.RPMSetpoint = header.rpmSetpoint;
    S.spinServoIdx = header.spinServoIdx;
    S.indexSign = header.indexSign;
    S.tableAdapter = header.tableAdapter != 0;
    S.markIdx = header.markIdx;
    S.markPoints.assign(positions, positions + header.markCount);
    sanitizeState(S);
    S.dirty = true;
    lastStateLoadResult = STATE_LOAD_OK;
    return true;
}
#endif

static void sanitizeState(SystemState& S)
{
    if (!isfinite(S.wheelIndex) || S.wheelIndex <= 0.0f || S.wheelIndex > 400.0f)
        S.wheelIndex = 96.0f;

    if (!isfinite(S.targetTwist)) S.targetTwist = 0.0f;
    while (S.targetTwist < 0.0f) S.targetTwist += S.wheelIndex;
    while (S.targetTwist >= S.wheelIndex) S.targetTwist -= S.wheelIndex;

    if (!isfinite(S.flowSetpoint)) S.flowSetpoint = 0.0f;
    S.flowSetpoint = constrain(S.flowSetpoint, 0.0f, 750.0f);
    S.RPMSetpoint = constrain(S.RPMSetpoint, 0, 200);
    if (S.zIdx < 0 || S.zIdx > 2) S.zIdx = 0;
    if (S.twistIdx < 0 || S.twistIdx > 2) S.twistIdx = 1;
    if (S.flow_dir < 0 || S.flow_dir > 3) S.flow_dir = 1;
    if (S.motorDir < 0 || S.motorDir > 3) S.motorDir = 1;
    S.spinServoIdx = S.spinServoIdx ? 1 : 0;
    S.indexSign = S.indexSign < 0 ? -1 : 1;
    S.zSign = S.zSign < 0 ? -1 : 1;
    S.tableAdapter = S.tableAdapter ? true : false;
    if (S.twistZeroRaw < 0 || S.twistZeroRaw > 4095) S.twistZeroRaw = 0;
    if (S.tipZeroRaw < 0 || S.tipZeroRaw >= 131072 || S.tipZeroRaw == 47850)
        S.tipZeroRaw = 82832; // Migrate obsolete default, preserve custom zeros.
    if (!isfinite(S.flowTicksToMlMin) || S.flowTicksToMlMin < 0.000001f ||
        S.flowTicksToMlMin > 10.0f) S.flowTicksToMlMin = 0.052f;
    S.indexSpinRpm = 0.0f;

    for (float& value : S.markPoints)
    {
        if (!isfinite(value)) value = 0.0f;
        while (value < 0.0f) value += S.wheelIndex;
        while (value >= S.wheelIndex) value -= S.wheelIndex;
    }

    if (S.markIdx < 0) S.markIdx = 0;
    if (!S.markPoints.empty() && size_t(S.markIdx) >= S.markPoints.size())
        S.markIdx = int(S.markPoints.size()) - 1;
    if (S.markPoints.empty()) S.markIdx = 0;
    S.targetValid = true;
}

#if STATE_HAS_RAW_FLASH
static bool writeStateRawFlash(const SavedStateV4Header& header,
                               const std::vector<float>& positions)
{
    if (positions.size() > STATE_MAX_SAVED_POSITIONS) return false;

    uint8_t page[FLASH_PAGE_SIZE];
    uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(STATE_FLASH_OFFSET, STATE_FLASH_BYTES);

    memset(page, 0xff, sizeof(page));
    memcpy(page, &header, sizeof(header));
    flash_range_program(STATE_FLASH_OFFSET, page, sizeof(page));

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(positions.data());
    size_t remaining = positions.size() * sizeof(float);
    uint32_t offset = STATE_FLASH_OFFSET + STATE_HEADER_BYTES;
    while (remaining > 0)
    {
        size_t chunk = remaining < sizeof(page) ? remaining : sizeof(page);
        memset(page, 0xff, sizeof(page));
        memcpy(page, bytes, chunk);
        flash_range_program(offset, page, sizeof(page));
        bytes += chunk;
        remaining -= chunk;
        offset += sizeof(page);
    }
    restore_interrupts(interrupts);

    const auto* storedHeader = reinterpret_cast<const SavedStateV4Header*>(
        XIP_BASE + STATE_FLASH_OFFSET);
    const float* storedPositions = reinterpret_cast<const float*>(
        XIP_BASE + STATE_FLASH_OFFSET + STATE_HEADER_BYTES);
    return storedHeader->magic == STATE_MAGIC &&
           storedHeader->checksum == stateChecksum(*storedHeader, storedPositions,
                                                    storedHeader->markCount);
}
#endif

inline bool saveState(SystemState& S)
{
    sanitizeState(S);
    lastStateBytesWritten = 0;
    lastStateSaveBackend = "rawflash";
    lastStateSavePath = STATE_FLASH_PATH;

#if !STATE_HAS_RAW_FLASH
    return false;
#else
    if (S.markPoints.size() > STATE_MAX_SAVED_POSITIONS) return false;

    SavedStateV4Header header = {};
    header.magic = STATE_MAGIC;
    header.version = STATE_VERSION;
    header.headerBytes = STATE_HEADER_BYTES;
    header.totalBytes = STATE_HEADER_BYTES + S.markPoints.size() * sizeof(float);
    header.targetTwist = S.targetTwist;
    header.flowSetpoint = S.flowSetpoint;
    header.wheelIndex = S.wheelIndex;
    header.zIdx = S.zIdx;
    header.twistIdx = S.twistIdx;
    header.flowDir = S.flow_dir;
    header.motorDir = S.motorDir;
    header.rpmSetpoint = S.RPMSetpoint;
    header.spinServoIdx = S.spinServoIdx;
    header.indexSign = S.indexSign;
    header.tableAdapter = S.tableAdapter ? 1 : 0;
    header.twistZeroRaw = S.twistZeroRaw;
    header.tipZeroRaw = S.tipZeroRaw;
    header.zSign = S.zSign;
    header.flowTicksToMlMin = S.flowTicksToMlMin;
    header.markIdx = S.markIdx;
    header.markCount = S.markPoints.size();
    header.checksum = stateChecksum(header, S.markPoints.data(), S.markPoints.size());

    if (!writeStateRawFlash(header, S.markPoints)) return false;
    lastStateBytesWritten = header.totalBytes;
    S.dirty = false;
    return true;
#endif
}

inline bool loadState(SystemState& S)
{
    lastStateLoadBackend = "rawflash";
    lastStateLoadPath = STATE_FLASH_PATH;

#if !STATE_HAS_RAW_FLASH
    lastStateLoadResult = STATE_LOAD_NO_FILE;
    return false;
#else
    const uint8_t* base = reinterpret_cast<const uint8_t*>(XIP_BASE + STATE_FLASH_OFFSET);
    const uint8_t* legacyBase = reinterpret_cast<const uint8_t*>(
        XIP_BASE + PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
    uint32_t magic = *reinterpret_cast<const uint32_t*>(base);
    if (magic == 0xffffffffUL || magic == 0x00000000UL)
    {
        if (loadLegacyStateAt(S, legacyBase)) return true;
        lastStateLoadResult = STATE_LOAD_NO_FILE;
        return false;
    }
    if (magic != STATE_MAGIC)
    {
        if (loadLegacyStateAt(S, legacyBase)) return true;
        lastStateLoadResult = STATE_LOAD_BAD_HEADER;
        return false;
    }

    uint16_t version = *reinterpret_cast<const uint16_t*>(base + sizeof(uint32_t));
    if (version == 2)
    {
        if (loadLegacyStateAt(S, base)) return true;
        lastStateLoadResult = STATE_LOAD_BAD_CHECKSUM;
        return false;
    }

    if (version == 3)
    {
        if (loadV3StateAt(S, base)) return true;
        lastStateLoadResult = STATE_LOAD_BAD_CHECKSUM;
        return false;
    }

    if (version != STATE_VERSION)
    {
        lastStateLoadResult = STATE_LOAD_BAD_HEADER;
        return false;
    }

    const SavedStateV4Header header =
        *reinterpret_cast<const SavedStateV4Header*>(base);
    if (header.headerBytes != STATE_HEADER_BYTES ||
        header.markCount > STATE_MAX_SAVED_POSITIONS ||
        header.totalBytes != STATE_HEADER_BYTES + header.markCount * sizeof(float) ||
        header.totalBytes > STATE_FLASH_BYTES)
    {
        lastStateLoadResult = STATE_LOAD_BAD_HEADER;
        return false;
    }

    const float* positions = reinterpret_cast<const float*>(base + STATE_HEADER_BYTES);
    if (header.checksum != stateChecksum(header, positions, header.markCount))
    {
        lastStateLoadResult = STATE_LOAD_BAD_CHECKSUM;
        return false;
    }

    S.targetTwist = header.targetTwist;
    S.flowSetpoint = header.flowSetpoint;
    S.wheelIndex = header.wheelIndex;
    S.zIdx = header.zIdx;
    S.twistIdx = header.twistIdx;
    S.flow_dir = header.flowDir;
    S.motorDir = header.motorDir;
    S.RPMSetpoint = header.rpmSetpoint;
    S.spinServoIdx = header.spinServoIdx;
    S.indexSign = header.indexSign;
    S.tableAdapter = header.tableAdapter != 0;
    S.twistZeroRaw = header.twistZeroRaw;
    S.tipZeroRaw = header.tipZeroRaw;
    S.zSign = header.zSign;
    S.flowTicksToMlMin = header.flowTicksToMlMin;
    S.markIdx = header.markIdx;
    S.markPoints.assign(positions, positions + header.markCount);
    sanitizeState(S);
    S.dirty = false;
    lastStateLoadResult = STATE_LOAD_OK;
    return true;
#endif
}
