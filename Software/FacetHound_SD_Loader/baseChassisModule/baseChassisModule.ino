#include <Arduino.h>
#include <math.h>
#include <strings.h>
#include <utility>
#include <SerialPIO.h>

#include "systemState.h"
#include "stateStore.h"
#include "motorControl.h"
#include "keyboardFunctions.h"
#include "sdGemLoader.h"
#include "gemGeometry.h"
#include "gemCache.h"

SerialPIO keysSerial(7, 6);
SerialPIO dispSerial(5, 4);
SerialPIO mastSerial(3, 2);

SystemState S;
GemSdDesign activeGemDesign;
GemRuntimeGeometry activeGemGeometry;
bool activeGemLoaded = false;
int lastJobCutSent = -1;
char activeGemCacheStatus[20] = "NONE";

static const float TWIST_ENCODER_COUNTS = 4096.0f;

static const float TIP_COUNTS_CLASSIC = 131072.0f;

static const uint32_t DISPLAY_BAUD = 460800;
static const uint32_t DISPLAY_FAST_PERIOD_MS = 20;
static const uint32_t SAVE_DEBOUNCE_MS = 500;
static const float MARK_MATCH_TOLERANCE = 0.0025f;

static const uint32_t TWIST_RX_TIMEOUT_MS = 120;
static const uint32_t TWIST_CHANGE_TIMEOUT_MS = 300;

char mastBuf[80];
uint8_t mastIdx = 0;

char keyBuf[16];
uint8_t keyIdx = 0;

char dispBuf[128];
uint8_t dispIdx = 0;

char usbBuf[128];
uint8_t usbIdx = 0;
uint32_t keyRxCount = 0;
uint32_t displayRxCount = 0;
uint32_t usbRxCount = 0;
bool usbStateStream = false;
uint32_t usbStatePeriodMs = 250;
uint32_t lastUsbStateMs = 0;

bool settingsMenuOpen = false;

// keyboardSettings.png programs the physical top-left key as HID A (usage 4).
static const uint8_t MENU_TOGGLE_KEY = 4;
static const uint8_t MENU_DELETE_KEY = 6;
// The former servo key is available for the second edit-tier direction.
static const uint8_t MENU_TIER_FINER_KEY = 11;
static const uint8_t TWIST_WHEEL_CCW_KEY = 16;
static const uint8_t TWIST_WHEEL_CLICK_KEY = 17;
static const uint8_t TWIST_WHEEL_CW_KEY = 18;

uint32_t lastDisplay = 0;
uint32_t lastSave = 0;
uint8_t displaySlot = 0;

uint32_t lastTwistRxMs = 0;
uint32_t lastTwistChangeMs = 0;
bool twistRxEver = false;
float lastTwistValue = NAN;

volatile uint32_t motorPgPulses = 0;
uint32_t lastRpmSampleMs = 0;

bool zEncoderValid = false;

void motorPgISR()
{
    motorPgPulses++;
}

static bool parseStrictFloat(const char* s, float* out)
{
    if (!s || !out)
        return false;

    char* endptr = nullptr;
    float v = strtof(s, &endptr);

    if (endptr == s || !isfinite(v))
        return false;

    while (*endptr == ' ' || *endptr == '\t')
        endptr++;

    if (*endptr != '\0')
        return false;

    *out = v;
    return true;
}

static bool parseStrictLong(const char* s, long* out)
{
    if (!s || !out)
        return false;

    char* endptr = nullptr;
    long v = strtol(s, &endptr, 10);

    if (endptr == s)
        return false;

    while (*endptr == ' ' || *endptr == '\t')
        endptr++;

    if (*endptr != '\0')
        return false;

    *out = v;
    return true;
}

static float wrapPositive(float v, float span)
{
    while (v < 0.0f)
        v += span;

    while (v >= span)
        v -= span;

    return v;
}

static void sendKV(const char* key, float v)
{
    dispSerial.print("@");
    dispSerial.print(key);
    dispSerial.print(",");
    dispSerial.println(v, 3);
}

static void sendKV(const char* key, int v)
{
    dispSerial.print("@");
    dispSerial.print(key);
    dispSerial.print(",");
    dispSerial.println(v);
}

static void updateTipDegreesFromEncoder()
{
    float countDelta = float(S.tipEncoder - S.tipZeroRaw);
    if (countDelta > TIP_COUNTS_CLASSIC * 0.5f) countDelta -= TIP_COUNTS_CLASSIC;
    if (countDelta < -TIP_COUNTS_CLASSIC * 0.5f) countDelta += TIP_COUNTS_CLASSIC;
    float degrees = countDelta * (360.0f / TIP_COUNTS_CLASSIC);
    S.tipDegrees = degrees - (S.tableAdapter ? 45.0f : 0.0f);
}

static void updateZMillimeters()
{
    S.zMM = float(S.zSign * S.zEncoderRaw) *
            (ZED_INDEX_INITS_PER_TURN /
             (ZED_GEAR_RATIO * ZED_ENC_OVERSAMPLE));
}

static float twistPositionFromRaw(int raw)
{
    float delta = float(raw - S.twistZeroRaw);
    float position = delta * S.wheelIndex / TWIST_ENCODER_COUNTS;
    if (S.indexSign < 0) position = -position;
    return wrapPositive(position, S.wheelIndex);
}

static bool targetMatchesMark(float target, float mark)
{
    if (S.wheelIndex <= 0.0f)
        return fabsf(target - mark) <= MARK_MATCH_TOLERANCE;

    return fabsf(shortestArcPath(target, mark, S.wheelIndex)) <=
           MARK_MATCH_TOLERANCE;
}

static int markTargetStatus()
{
    if (S.markPoints.empty())
        return 2;

    if (S.markIdx >= 0 &&
        size_t(S.markIdx) < S.markPoints.size() &&
        targetMatchesMark(S.targetTwist, S.markPoints[S.markIdx]))
    {
        return 0;
    }

    for (size_t i = 0; i < S.markPoints.size(); i++)
    {
        if (targetMatchesMark(S.targetTwist, S.markPoints[i]))
            return 1;
    }

    return 2;
}

void mastTask()
{
    while (mastSerial.available())
    {
        char c = mastSerial.read();
        S.mastRxCount++;

        if (c == '\r')
            continue;

        if (c == '\n')
        {
            mastBuf[mastIdx] = '\0';
            mastIdx = 0;

            if (mastBuf[0] != '@')
                continue;

            char* comma = strchr(mastBuf, ',');
            if (!comma)
                continue;

            *comma = '\0';

            const char* key = mastBuf + 1;
            const char* valueText = comma + 1;

            float val = 0.0f;

            if (!parseStrictFloat(valueText, &val))
                continue;

            if (!strcmp(key, "tip"))
            {
                S.tipEncoder = int(val);

                updateTipDegreesFromEncoder();

                checkServoStatus(&S);
            }
            else if (!strcmp(key, "twist"))
            {
                if (val < 0.0f || val > 4095.0f)
                    continue;

                uint32_t now = millis();

                lastTwistRxMs = now;
                twistRxEver = true;

                S.twistEncoderRaw = int(val);
                float nextTwist = twistPositionFromRaw(S.twistEncoderRaw);

                if (!isfinite(lastTwistValue) ||
                    fabsf(shortestArcPath(nextTwist, lastTwistValue, S.wheelIndex)) > 0.001f)
                {
                    lastTwistValue = nextTwist;
                    lastTwistChangeMs = now;
                }

                S.actualTwist = nextTwist;

                if (!S.twistReady)
                {
                    S.twistReady = true;
                    lastTwistChangeMs = now;

                    if (!S.targetValid)
                    {
                        S.targetTwist = S.actualTwist;
                        S.targetValid = true;
                    }
                }
            }
            else if (!strcmp(key, "force"))
            {
                if (val < 0.0f || val > 140000.0f)
                    continue;

                S.forceValue = val;
            }
        }
        else if (mastIdx < sizeof(mastBuf) - 1)
        {
            mastBuf[mastIdx++] = c;
        }
        else
        {
            mastIdx = 0;
        }
    }
}

void twistWatchdogTask()
{
    // Classic motion never cancelled twist moves based on serial timing.
    // Keep this task inert so encoder/display cadence cannot starve motion.
}

static void sendDisplayLine(const char* line)
{
    dispSerial.println(line);
}

static void sendCfgText(const char* id, const char* value)
{
    dispSerial.print("@CFG,");
    dispSerial.print(id);
    dispSerial.print(",");
    dispSerial.println(value);
}

static void sendCfgFloat(const char* id, float value)
{
    dispSerial.print("@CFG,");
    dispSerial.print(id);
    dispSerial.print(",");
    dispSerial.println(value, 6);
}

static void sendCfgCount(const char* id, size_t value)
{
    dispSerial.print("@CFG,");
    dispSerial.print(id);
    dispSerial.print(",");
    dispSerial.println(static_cast<unsigned long>(value));
}

static void sendCfgAck(const char* id, const char* value)
{
    dispSerial.print("@CFGACK,");
    dispSerial.print(id);
    dispSerial.print(",");
    dispSerial.println(value);
}

static void sendCfgNak(const char* id, const char* reason)
{
    dispSerial.print("@CFGNAK,");
    dispSerial.print(id);
    dispSerial.print(",");
    dispSerial.println(reason);
}

static void safeProtocolText(const char* input, char* output, size_t outputSize)
{
    if (!output || !outputSize) return;
    size_t written = 0;
    while (input && *input && written + 1 < outputSize)
    {
        char c = *input++;
        if (c == ',' || c == '\r' || c == '\n') c = ' ';
        output[written++] = c;
    }
    output[written] = '\0';
}

static void sendActiveCut(bool force = false)
{
    if (!activeGemLoaded || S.markIdx < 0 ||
        size_t(S.markIdx) >= activeGemDesign.cuts.size())
        return;
    if (!force && lastJobCutSent == S.markIdx) return;

    const GemCutCoordinate& cut = activeGemDesign.cuts[size_t(S.markIdx)];
    char name[GEM_SD_FACET_NAME_LENGTH] = {};
    safeProtocolText(cut.name, name, sizeof(name));
    dispSerial.print("@JOB,");
    dispSerial.print(cut.tier);
    dispSerial.print(",");
    dispSerial.print(cut.facet);
    dispSerial.print(",");
    dispSerial.print(cut.angleDegrees, 4);
    dispSerial.print(",");
    dispSerial.print(cut.gemcadDistance, 7);
    dispSerial.print(",");
    dispSerial.print(cut.index, 4);
    dispSerial.print(",");
    dispSerial.println(name);
    lastJobCutSent = S.markIdx;
}

static void sendActiveMesh()
{
    if (!activeGemLoaded || activeGemGeometry.vertices.empty())
    {
        sendDisplayLine("@MESHCLEAR,0");
        return;
    }

    dispSerial.print("@MESHBEGIN,");
    dispSerial.print(activeGemGeometry.vertices.size());
    dispSerial.print(",");
    dispSerial.print(activeGemGeometry.edges.size());
    dispSerial.print(",");
    dispSerial.print(activeGemGeometry.planes.size());
    dispSerial.print(",");
    dispSerial.print(activeGemDesign.wheelIndex, 4);
    dispSerial.print(",");
    dispSerial.println(activeGemGeometry.radius, 7);

    for (size_t i = 0; i < activeGemGeometry.vertices.size(); ++i)
    {
        const GemGeometryVertex& vertex = activeGemGeometry.vertices[i];
        dispSerial.print("@MESHV,");
        dispSerial.print(i);
        dispSerial.print(",");
        dispSerial.print(vertex.x, 7);
        dispSerial.print(",");
        dispSerial.print(vertex.y, 7);
        dispSerial.print(",");
        dispSerial.println(vertex.z, 7);
    }

    for (size_t i = 0; i < activeGemGeometry.edges.size(); ++i)
    {
        const GemGeometryEdge& edge = activeGemGeometry.edges[i];
        dispSerial.print("@MESHE,");
        dispSerial.print(i);
        dispSerial.print(",");
        dispSerial.print(edge.a);
        dispSerial.print(",");
        dispSerial.print(edge.b);
        dispSerial.print(",");
        dispSerial.print(edge.supportCount);
        for (uint8_t support = 0; support < edge.supportCount; ++support)
        {
            dispSerial.print(",");
            dispSerial.print(edge.supportPlanes[support]);
        }
        dispSerial.println();
    }

    for (size_t i = 0; i < activeGemGeometry.planes.size(); ++i)
    {
        const GemGeometryPlane& plane = activeGemGeometry.planes[i];
        dispSerial.print("@MESHP,");
        dispSerial.print(i);
        dispSerial.print(",");
        dispSerial.print(plane.angleDegrees, 5);
        dispSerial.print(",");
        dispSerial.print(plane.index, 5);
        dispSerial.print(",");
          dispSerial.print(plane.tier);
          dispSerial.print(",");
          dispSerial.print(plane.facet);
          dispSerial.print(",");
          char name[GEM_SD_FACET_NAME_LENGTH] = {};
          safeProtocolText(plane.name, name, sizeof(name));
          dispSerial.println(name);
      }
    sendDisplayLine("@MESHEND,1");
}

static void sendSdFilePage(size_t start, size_t requested)
{
    if (!gemSdReady())
    {
        sendCfgText("SD_STATUS", "NO_CARD");
        sendCfgCount("SD_FILE_COUNT", 0);
        return;
    }

    const size_t pageLimit = 6;
    size_t count = requested > pageLimit ? pageLimit : requested;
    size_t total = gemSdFileCount();
    if (start > total) start = total;
    if (count > total - start) count = total - start;
    sendCfgText("SD_STATUS", "READY");
    sendCfgCount("SD_FILE_COUNT", total);
    for (size_t i = 0; i < count; ++i)
    {
        char fileName[GEM_SD_FILE_NAME_LENGTH] = {};
        char safeName[GEM_SD_FILE_NAME_LENGTH] = {};
        char id[24] = {};
        if (!gemSdFileNameAt(start + i, fileName, sizeof(fileName))) continue;
        safeProtocolText(fileName, safeName, sizeof(safeName));
        snprintf(id, sizeof(id), "SD_FILE_%lu",
                 static_cast<unsigned long>(start + i));
        sendCfgText(id, safeName);
    }
}

static void applyLoadedGemDesign(GemSdDesign&& design, GemRuntimeGeometry&& geometry)
{
    hardStopTwist(S);
    updateWheelIndex(S, design.wheelIndex);

    S.markPoints.clear();
    S.markPoints.reserve(design.cuts.size());
    for (const GemCutCoordinate& cut : design.cuts)
        S.markPoints.push_back(wrapPositive(cut.index, S.wheelIndex));

    S.markIdx = 0;
    S.targetTwist = S.markPoints.front();
    S.targetValid = true;
    notifyTwistTargetChanged();
    S.dirty = true;

    activeGemDesign = std::move(design);
    activeGemGeometry = std::move(geometry);
    activeGemLoaded = true;
    lastJobCutSent = -1;
}

static void sendPositionPage(size_t start, size_t requested)
{
    const size_t pageLimit = 8;
    size_t count = requested > pageLimit ? pageLimit : requested;
    if (start > S.markPoints.size()) start = S.markPoints.size();
    if (count > S.markPoints.size() - start) count = S.markPoints.size() - start;

    dispSerial.print("@CFGSYNC,POSITIONS_BEGIN,");
    dispSerial.print(static_cast<unsigned long>(start));
    dispSerial.print(",");
    dispSerial.println(static_cast<unsigned long>(count));

    for (size_t i = 0; i < count; ++i)
    {
        dispSerial.print("@CFG,POSITION_");
        dispSerial.print(static_cast<unsigned long>(start + i));
        dispSerial.print(",");
        dispSerial.println(S.markPoints[start + i], 4);
    }
    sendDisplayLine("@CFGSYNC,POSITIONS_END");
}

static void sendConfigSnapshot()
{
    sendDisplayLine("@CFGSYNC,BEGIN");
    sendCfgText("INDEX_DIRECTION", S.indexSign < 0 ? "CCW" : "CW");
    sendCfgText("TABLE_ADAPTER", S.tableAdapter ? "ON" : "OFF");
    sendCfgText("SERVO_ENABLED", S.spinServoIdx ? "ON" : "OFF");
    sendCfgFloat("WHEEL_INDEX", S.wheelIndex);
    sendCfgText("Z_POLARITY", S.zSign < 0 ? "REVERSED" : "NORMAL");
    sendCfgFloat("FLOW_CONVERSION", S.flowTicksToMlMin);
    sendCfgFloat("INDEX_SPIN_RPM", S.indexSpinRpm);
    sendCfgCount("POSITION_COUNT", S.markPoints.size());
    sendCfgCount("MESH_VERTEX_COUNT", activeGemGeometry.vertices.size());
    sendCfgCount("MESH_EDGE_COUNT", activeGemGeometry.edges.size());
    sendCfgCount("MESH_PLANE_COUNT", activeGemGeometry.planes.size());
    sendCfgText("SD_STATUS", gemSdReady() ? "READY" : "NO_CARD");
    sendCfgText("GEM_CACHE", activeGemCacheStatus);
    if (activeGemLoaded)
    {
        char title[GEM_SD_TITLE_LENGTH] = {};
        safeProtocolText(activeGemDesign.title, title, sizeof(title));
        sendCfgText("SD_ACTIVE", title);
        sendCfgCount("SD_CUT_COUNT", activeGemDesign.cuts.size());
        sendCfgCount("SD_TIER_COUNT", activeGemDesign.tierCount);
    }
    else
    {
        sendCfgText("SD_ACTIVE", "NONE");
        sendCfgCount("SD_CUT_COUNT", 0);
        sendCfgCount("SD_TIER_COUNT", 0);
    }
    sendDisplayLine("@CFGSYNC,END");
}

static bool splitSetting(char* text, char** id, char** value)
{
    if (!text || !id || !value) return false;
    char* comma = strchr(text, ',');
    if (!comma) return false;
    *comma = '\0';
    *id = text;
    *value = comma + 1;
    return **id && **value;
}

static void wrapAllPositions()
{
    for (float& position : S.markPoints)
        position = wrapPositive(position, S.wheelIndex);
    S.targetTwist = wrapPositive(S.targetTwist, S.wheelIndex);
}

static void applyConfigSet(char* settingText)
{
    char* id = nullptr;
    char* value = nullptr;
    if (!splitSetting(settingText, &id, &value))
    {
        sendCfgNak("UNKNOWN", "FORMAT");
        return;
    }

    if (!strcmp(id, "DISPLAY_MODE"))
    {
        sendCfgAck(id, value);
        return;
    }
    if (!strcmp(id, "INDEX_DIRECTION"))
    {
        int nextSign = !strcasecmp(value, "CCW") ? -1 :
                       !strcasecmp(value, "CW") ? 1 : 0;
        if (!nextSign)
        {
            sendCfgNak(id, "EXPECTED_CW_OR_CCW");
            return;
        }
        if (nextSign != S.indexSign)
        {
            hardStopTwist(S);
            S.indexSign = nextSign;
            S.twistReady = false;
            S.targetValid = false;
            lastTwistValue = NAN;
            S.dirty = true;
        }
        sendCfgAck(id, nextSign < 0 ? "CCW" : "CW");
        return;
    }
    if (!strcmp(id, "TABLE_ADAPTER"))
    {
        if (strcasecmp(value, "ON") && strcasecmp(value, "OFF"))
        {
            sendCfgNak(id, "EXPECTED_ON_OR_OFF");
            return;
        }
        S.tableAdapter = !strcasecmp(value, "ON");
        updateTipDegreesFromEncoder();
        S.dirty = true;
        sendCfgAck(id, S.tableAdapter ? "ON" : "OFF");
        return;
    }
    if (!strcmp(id, "SERVO_ENABLED"))
    {
        if (strcasecmp(value, "ON") && strcasecmp(value, "OFF"))
        {
            sendCfgNak(id, "EXPECTED_ON_OR_OFF");
            return;
        }
        setSpinServoEnabled(&S, !strcasecmp(value, "ON"));
        sendCfgAck(id, S.spinServoIdx ? "ON" : "OFF");
        return;
    }
    if (!strcmp(id, "WHEEL_INDEX"))
    {
        float wheel = 0.0f;
        if (!parseStrictFloat(value, &wheel) || wheel <= 0.0f || wheel > 400.0f)
        {
            sendCfgNak(id, "OUT_OF_RANGE");
            return;
        }
        updateWheelIndex(S, wheel);
        wrapAllPositions();
        S.dirty = true;
        sendCfgAck(id, value);
        return;
    }
    if (!strcmp(id, "Z_POLARITY"))
    {
        int nextSign = !strcasecmp(value, "REVERSED") ? -1 :
                       !strcasecmp(value, "NORMAL") ? 1 : 0;
        if (!nextSign)
        {
            sendCfgNak(id, "EXPECTED_NORMAL_OR_REVERSED");
            return;
        }
        hardStopZ();
        S.zSign = nextSign;
        updateZMillimeters();
        S.dirty = true;
        sendCfgAck(id, S.zSign < 0 ? "REVERSED" : "NORMAL");
        return;
    }
    if (!strcmp(id, "FLOW_CONVERSION"))
    {
        float conversion = 0.0f;
        if (!parseStrictFloat(value, &conversion) || conversion < 0.000001f || conversion > 10.0f)
        {
            sendCfgNak(id, "OUT_OF_RANGE");
            return;
        }
        S.flowTicksToMlMin = conversion;
        S.dirty = true;
        sendCfgAck(id, value);
        return;
    }
    if (!strncmp(id, "POSITION_", 9))
    {
        long index = -1;
        float position = 0.0f;
        if (!parseStrictLong(id + 9, &index) || index < 0 ||
            size_t(index) >= S.markPoints.size() ||
            !parseStrictFloat(value, &position))
        {
            sendCfgNak(id, "BAD_POSITION");
            return;
        }
        position = wrapPositive(position, S.wheelIndex);
        updatePositionInList(&S, int(index), position);
        sendCfgAck(id, value);
        return;
    }

    sendCfgNak(id, "UNKNOWN_SETTING");
}

static bool isAllowedSpacing(float spacing)
{
    return spacing == 4.0f || spacing == 8.0f || spacing == 12.0f ||
           spacing == 16.0f || spacing == 32.0f;
}

static void applyConfigAction(char* actionText)
{
    char* action = nullptr;
    char* value = nullptr;
    if (!splitSetting(actionText, &action, &value))
    {
        sendCfgNak("ACTION", "FORMAT");
        return;
    }

    if (!strcmp(action, "RESET_POSITIONS"))
    {
        float spacing = 0.0f;
        if (!parseStrictFloat(value, &spacing) || !isAllowedSpacing(spacing))
        {
            sendCfgNak(action, "EXPECTED_4_8_12_16_32");
            return;
        }
        resetMarkPointsWithSpacing(&S, spacing);
        sendCfgAck(action, value);
        sendCfgCount("POSITION_COUNT", S.markPoints.size());
        sendPositionPage(0, 8);
        return;
    }
    if (!strcmp(action, "ZERO_ENCODER"))
    {
        bool all = !strcasecmp(value, "ALL");
        if (!all && strcasecmp(value, "INDEX") && strcasecmp(value, "TIP") && strcasecmp(value, "Z"))
        {
            sendCfgNak(action, "EXPECTED_INDEX_TIP_Z_ALL");
            return;
        }
        if (all || !strcasecmp(value, "INDEX"))
        {
            hardStopTwist(S);
            S.twistZeroRaw = S.twistEncoderRaw;
            S.actualTwist = 0.0f;
            S.targetTwist = 0.0f;
            S.targetValid = true;
            S.twistReady = true;
            lastTwistValue = 0.0f;
        }
        if (all || !strcasecmp(value, "TIP"))
        {
            const int adapterCounts = S.tableAdapter ? int(TIP_COUNTS_CLASSIC / 8.0f) : 0;
            S.tipZeroRaw = S.tipEncoder - adapterCounts;
            while (S.tipZeroRaw < 0) S.tipZeroRaw += int(TIP_COUNTS_CLASSIC);
            while (S.tipZeroRaw >= int(TIP_COUNTS_CLASSIC)) S.tipZeroRaw -= int(TIP_COUNTS_CLASSIC);
            updateTipDegreesFromEncoder();
        }
        if (all || !strcasecmp(value, "Z"))
        {
            hardStopZ();
            S.zEncoderRaw = 0;
            S.zMM = 0.0f;
            zEncoderValid = false;
            sendDisplayLine("@ZRESET,0");
        }
        S.dirty = true;
        sendCfgAck(action, value);
        return;
    }
    if (!strcmp(action, "INDEX_SPIN_START"))
    {
        float rpm = 0.0f;
        if (!parseStrictFloat(value, &rpm) || fabsf(rpm) < 0.01f || fabsf(rpm) > 10.0f)
        {
            sendCfgNak(action, "RPM_RANGE_0.01_TO_10");
            return;
        }
        if (fabsf(S.indexSpinRpm) < 0.0001f)
            hardStopTwist(S);
        S.indexSpinRpm = rpm;
        S.indexSpinLastCommandMs = millis();
        sendCfgAck(action, value);
        return;
    }
    if (!strcmp(action, "INDEX_SPIN_STOP"))
    {
        hardStopTwist(S);
        sendCfgAck(action, "STOPPED");
        return;
    }
    if (!strcmp(action, "ADD_POSITION"))
    {
        float position = 0.0f;
        if (!parseStrictFloat(value, &position))
        {
            sendCfgNak(action, "BAD_POSITION");
            return;
        }
        position = wrapPositive(position, S.wheelIndex);
        addPositionToList(&S, position);
        sendCfgAck(action, value);
        sendCfgCount("POSITION_COUNT", S.markPoints.size());
        return;
    }
    if (!strcmp(action, "DELETE_POSITION"))
    {
        long index = -1;
        if (!parseStrictLong(value, &index) || index < 0 ||
            size_t(index) >= S.markPoints.size())
        {
            sendCfgNak(action, "BAD_INDEX");
            return;
        }
        deletePositionInList(&S, int(index));
        sendCfgAck(action, value);
        sendCfgCount("POSITION_COUNT", S.markPoints.size());
        return;
    }
    if (!strcmp(action, "LOAD_SD_FILE"))
    {
        long index = -1;
        if (!parseStrictLong(value, &index) || index < 0)
        {
            sendCfgNak(action, "BAD_INDEX");
            return;
        }

        char sourcePath[GEM_SD_FILE_NAME_LENGTH] = {};
        if (!gemSdFilePathAt(size_t(index), sourcePath, sizeof(sourcePath)))
        {
            sendCfgNak(action, "OPEN_FAILED");
            return;
        }

        GemSdDesign candidate;
        GemRuntimeGeometry candidateGeometry;
        GemCacheResult cacheResult = loadGemCache(sourcePath, &candidate,
                                                  &candidateGeometry);
        if (cacheResult == GemCacheResult::OK)
        {
            snprintf(activeGemCacheStatus, sizeof(activeGemCacheStatus), "LOADED");
        }
        else
        {
            GemSdResult result = loadGemSdFileAt(size_t(index), &candidate);
            if (result != GemSdResult::OK)
            {
                sendCfgNak(action, gemSdResultText(result));
                return;
            }

            GemGeometryResult geometryResult = buildGemGeometry(candidate,
                                                                 &candidateGeometry);
            if (geometryResult != GemGeometryResult::OK)
            {
                sendCfgNak(action, gemGeometryResultText(geometryResult));
                return;
            }

            GemCacheResult saveResult = saveGemCache(sourcePath, candidate,
                                                      candidateGeometry);
            snprintf(activeGemCacheStatus, sizeof(activeGemCacheStatus),
                     saveResult == GemCacheResult::OK ? "BUILT_SAVED" : "BUILT_ONLY");
            Serial.print("CACHE ");
            Serial.print(gemCacheResultText(cacheResult));
            Serial.print(" -> ");
            Serial.println(gemCacheResultText(saveResult));
        }

        applyLoadedGemDesign(std::move(candidate), std::move(candidateGeometry));
        char title[GEM_SD_TITLE_LENGTH] = {};
        safeProtocolText(activeGemDesign.title, title, sizeof(title));
        sendCfgAck(action, title);
        sendCfgText("SD_ACTIVE", title);
        sendCfgCount("SD_CUT_COUNT", activeGemDesign.cuts.size());
        sendCfgCount("SD_TIER_COUNT", activeGemDesign.tierCount);
        sendCfgText("GEM_CACHE", activeGemCacheStatus);
        sendCfgFloat("WHEEL_INDEX", S.wheelIndex);
        sendCfgCount("POSITION_COUNT", S.markPoints.size());
        sendActiveCut(true);
        sendActiveMesh();
        return;
    }
    sendCfgNak(action, "UNKNOWN_ACTION");
}

void displayRxTask()
{
    while (dispSerial.available())
    {
        char c = dispSerial.read();
        displayRxCount++;

        if (c == '\r')
            continue;

        if (c == '\n')
        {
            dispBuf[dispIdx] = '\0';
            dispIdx = 0;

            if (dispBuf[0] != '@')
                continue;

            char* comma = strchr(dispBuf, ',');
            if (!comma) continue;
            *comma = '\0';
            const char* key = dispBuf + 1;
            char* valueText = comma + 1;

            if (!strcmp(key, "ZENC"))
            {
                long raw = 0;

                if (!parseStrictLong(valueText, &raw))
                    continue;

                if (zEncoderValid && raw == 0 && abs(S.zEncoderRaw) > 5)
                    continue;

                S.zEncoderRaw = int(raw);
                zEncoderValid = true;

                updateZMillimeters();
            }
            else if (!strcmp(key, "MENU"))
            {
                settingsMenuOpen = !strcasecmp(valueText, "OPEN") ||
                                   !strcmp(valueText, "1");
            }
            else if (!strcmp(key, "CFGGET"))
            {
                if (!strcasecmp(valueText, "ALL"))
                {
                    sendConfigSnapshot();
                }
                else if (!strcasecmp(valueText, "MESH"))
                {
                    sendActiveMesh();
                }
                else if (!strncasecmp(valueText, "POSITIONS,", 10))
                {
                    char* range = valueText + 10;
                    char* startText = nullptr;
                    char* countText = nullptr;
                    if (splitSetting(range, &startText, &countText))
                    {
                        long start = 0;
                        long count = 0;
                        if (parseStrictLong(startText, &start) &&
                            parseStrictLong(countText, &count) &&
                            start >= 0 && count > 0)
                            sendPositionPage(size_t(start), size_t(count));
                    }
                }
                else if (!strncasecmp(valueText, "SD_FILES,", 9))
                {
                    char* range = valueText + 9;
                    char* startText = nullptr;
                    char* countText = nullptr;
                    if (splitSetting(range, &startText, &countText))
                    {
                        long start = 0;
                        long count = 0;
                        if (parseStrictLong(startText, &start) &&
                            parseStrictLong(countText, &count) &&
                            start >= 0 && count > 0)
                            sendSdFilePage(size_t(start), size_t(count));
                    }
                }
            }
            else if (!strcmp(key, "CFGSET"))
            {
                applyConfigSet(valueText);
            }
            else if (!strcmp(key, "CFGACTION"))
            {
                applyConfigAction(valueText);
            }
        }
        else if (dispIdx < sizeof(dispBuf) - 1)
        {
            dispBuf[dispIdx++] = c;
        }
        else
        {
            dispIdx = 0;
        }
    }
}

static void sendUsbState()
{
    Serial.print("@STATE,");
    Serial.print(millis());
    Serial.print(",tip="); Serial.print(S.tipDegrees, 3);
    Serial.print(",target="); Serial.print(S.targetTwist, 4);
    Serial.print(",actual="); Serial.print(S.actualTwist, 4);
    Serial.print(",error="); Serial.print(S.twistError, 4);
    Serial.print(",z_mm="); Serial.print(S.zMM, 4);
    Serial.print(",rpm_set="); Serial.print(S.RPMSetpoint);
    Serial.print(",rpm_actual="); Serial.print(S.RPMValue);
    Serial.print(",flow="); Serial.print(S.flowSetpoint, 2);
    Serial.print(",force="); Serial.print(S.forceValue, 1);
    Serial.print(",twist_lock="); Serial.print(S.twistLock);
    Serial.print(",z_lock="); Serial.print(S.zLock);
    Serial.print(",motor_dir="); Serial.print(S.motorDir);
    Serial.print(",flow_dir="); Serial.print(S.flow_dir);
    Serial.print(",mark="); Serial.print(S.markIdx + 1);
    Serial.print("/"); Serial.print(S.markPoints.size());
    Serial.print(",sd="); Serial.print(gemSdReady() ? "ready" : "missing");
    Serial.print(",gem=");
    Serial.println(activeGemLoaded ? activeGemDesign.fileName : "none");

    Serial.print("@IO,mast_rx="); Serial.print(S.mastRxCount);
    Serial.print(",key_rx="); Serial.print(keyRxCount);
    Serial.print(",display_rx="); Serial.print(displayRxCount);
    Serial.print(",usb_rx="); Serial.println(usbRxCount);
}

static void stopAllFromUsb()
{
    hardStopTwist(S);
    S.zLock = 0;
    hardStopZ();
    S.RPMSetpoint = 0;
    S.flowSetpoint = 0.0f;
    S.flow_dir = 1;
    S.dirty = true;
}

static void printUsbHelp()
{
    Serial.println("@HELP,STATUS | STREAM ON [ms] | STREAM OFF");
    Serial.println("@HELP,KEY <hid-code> | JOG TWIST <index-units> | JOG Z <steps>");
    Serial.println("@HELP,RPM <0..200> | MOTOR CW|CCW|OFF | FLOW <0..750>");
    Serial.println("@HELP,PUMP FWD|REV|OFF | STOP | HELP");
}

static void routeKeyboardKey(uint8_t key)
{
    if (!settingsMenuOpen && key == MENU_TOGGLE_KEY)
    {
        sendDisplayLine("@MENU,1");
    }
    else if (settingsMenuOpen)
    {
        if (key == MENU_TOGGLE_KEY)
            sendDisplayLine("@MENUKEY,BACK");
        else if (key == MENU_DELETE_KEY)
            sendDisplayLine("@MENUKEY,DELETE");
        else if (key == MENU_TIER_FINER_KEY)
            sendDisplayLine("@MENUKEY,FINER");
        else if (key == TWIST_WHEEL_CCW_KEY)
            sendDisplayLine("@MENUKEY,UP");
        else if (key == TWIST_WHEEL_CW_KEY)
            sendDisplayLine("@MENUKEY,DOWN");
        else if (key == TWIST_WHEEL_CLICK_KEY)
            sendDisplayLine("@MENUKEY,SELECT");
        // Machine-motion keys are intentionally suppressed in menus.
    }
    else
    {
        handleKey(&S, key);
    }
}

static void handleUsbCommand(char* line)
{
    char* save = nullptr;
    char* command = strtok_r(line, " \t", &save);
    if (!command) return;

    if (!strcasecmp(command, "HELP"))
    {
        printUsbHelp();
        return;
    }
    if (!strcasecmp(command, "STATUS"))
    {
        sendUsbState();
        return;
    }
    if (!strcasecmp(command, "STREAM"))
    {
        char* mode = strtok_r(nullptr, " \t", &save);
        if (mode && !strcasecmp(mode, "ON"))
        {
            long period = long(usbStatePeriodMs);
            char* periodText = strtok_r(nullptr, " \t", &save);
            if (periodText && !parseStrictLong(periodText, &period)) period = 250;
            usbStatePeriodMs = uint32_t(constrain(period, 50L, 5000L));
            usbStateStream = true;
            lastUsbStateMs = 0;
            Serial.println("@ACK,STREAM,ON");
        }
        else if (mode && !strcasecmp(mode, "OFF"))
        {
            usbStateStream = false;
            Serial.println("@ACK,STREAM,OFF");
        }
        else Serial.println("@ERR,STREAM,expected ON [ms] or OFF");
        return;
    }
    if (!strcasecmp(command, "KEY"))
    {
        long key = -1;
        char* value = strtok_r(nullptr, " \t", &save);
        if (!value || !parseStrictLong(value, &key) || key < 0 || key > 255)
            Serial.println("@ERR,KEY,expected HID code 0..255");
        else
        {
            routeKeyboardKey(uint8_t(key));
            Serial.print("@ACK,KEY,"); Serial.println(key);
        }
        return;
    }
    if (!strcasecmp(command, "JOG"))
    {
        char* axis = strtok_r(nullptr, " \t", &save);
        char* valueText = strtok_r(nullptr, " \t", &save);
        if (!axis || !valueText)
        {
            Serial.println("@ERR,JOG,expected TWIST <units> or Z <steps>");
            return;
        }
        if (!strcasecmp(axis, "TWIST"))
        {
            float delta = 0.0f;
            if (!parseStrictFloat(valueText, &delta))
            {
                Serial.println("@ERR,JOG,bad twist delta");
                return;
            }
            const float start = S.twistReady ? S.actualTwist : S.targetTwist;
            S.targetTwist = wrapPositive(start + delta, S.wheelIndex);
            S.targetValid = true;
            S.twistLock = 1;
            notifyTwistTargetChanged();
            S.dirty = true;
            Serial.print("@ACK,JOG,TWIST,"); Serial.println(S.targetTwist, 4);
        }
        else if (!strcasecmp(axis, "Z"))
        {
            long steps = 0;
            if (!parseStrictLong(valueText, &steps) || steps == 0)
            {
                Serial.println("@ERR,JOG,bad Z steps");
                return;
            }
            S.zLock = 1;
            requestZMove(steps * S.zSign);
            S.dirty = true;
            Serial.print("@ACK,JOG,Z,"); Serial.println(steps);
        }
        else Serial.println("@ERR,JOG,unknown axis");
        return;
    }
    if (!strcasecmp(command, "RPM") || !strcasecmp(command, "FLOW"))
    {
        float value = 0.0f;
        char* valueText = strtok_r(nullptr, " \t", &save);
        if (!valueText || !parseStrictFloat(valueText, &value))
        {
            Serial.println("@ERR,VALUE,bad number");
            return;
        }
        if (!strcasecmp(command, "RPM"))
        {
            S.RPMSetpoint = constrain(int(lroundf(value)), RPM_LIMIT_LOW, RPM_LIMIT_HIGH);
            Serial.print("@ACK,RPM,"); Serial.println(S.RPMSetpoint);
        }
        else
        {
            S.flowSetpoint = constrain(value, 0.0f, 750.0f);
            Serial.print("@ACK,FLOW,"); Serial.println(S.flowSetpoint, 2);
        }
        S.dirty = true;
        return;
    }
    if (!strcasecmp(command, "MOTOR") || !strcasecmp(command, "PUMP"))
    {
        char* mode = strtok_r(nullptr, " \t", &save);
        if (!mode)
        {
            Serial.println("@ERR,MODE,missing direction");
            return;
        }
        if (!strcasecmp(command, "MOTOR"))
        {
            if (!strcasecmp(mode, "CW")) S.motorDir = 1;
            else if (!strcasecmp(mode, "CCW")) S.motorDir = 3;
            else if (!strcasecmp(mode, "OFF")) S.RPMSetpoint = 0;
            else { Serial.println("@ERR,MOTOR,expected CW|CCW|OFF"); return; }
            Serial.println("@ACK,MOTOR");
        }
        else
        {
            if (!strcasecmp(mode, "FWD")) S.flow_dir = 2;
            else if (!strcasecmp(mode, "REV")) S.flow_dir = 0;
            else if (!strcasecmp(mode, "OFF")) S.flow_dir = 1;
            else { Serial.println("@ERR,PUMP,expected FWD|REV|OFF"); return; }
            Serial.println("@ACK,PUMP");
        }
        S.dirty = true;
        return;
    }
    if (!strcasecmp(command, "STOP"))
    {
        stopAllFromUsb();
        Serial.println("@ACK,STOP");
        return;
    }
    Serial.println("@ERR,UNKNOWN,use HELP");
}

void usbDiagnosticTask()
{
    while (Serial.available())
    {
        char c = char(Serial.read());
        usbRxCount++;
        if (c == '\r') continue;
        if (c == '\n')
        {
            usbBuf[usbIdx] = '\0';
            if (usbIdx) handleUsbCommand(usbBuf);
            usbIdx = 0;
        }
        else if (usbIdx < sizeof(usbBuf) - 1)
            usbBuf[usbIdx++] = c;
        else
            usbIdx = 0;
    }

    const uint32_t now = millis();
    if (usbStateStream && now - lastUsbStateMs >= usbStatePeriodMs)
    {
        lastUsbStateMs = now;
        sendUsbState();
    }
}

void keyboardTask()
{
    while (keysSerial.available())
    {
        char c = keysSerial.read();
        keyRxCount++;

        if (c == '\r')
            continue;

        if (c == '\n')
        {
            keyBuf[keyIdx] = '\0';
            keyIdx = 0;

            if (keyBuf[0] == 'D')
            {
                int key = atoi(&keyBuf[2]);
                Serial.print("@KEY,"); Serial.println(key);

                routeKeyboardKey(uint8_t(key));
            }
        }
        else if (keyIdx < sizeof(keyBuf) - 1)
        {
            keyBuf[keyIdx++] = c;
        }
        else
        {
            keyIdx = 0;
        }
    }
}

void rpmTask()
{
#if USE_LAP_MOTOR_RS485
    // Actual RPM comes from BLD-510B register 0x8018 in RS-485 mode.
    return;
#else
    uint32_t now = millis();

    if (lastRpmSampleMs == 0)
    {
        lastRpmSampleMs = now;
        return;
    }

    uint32_t dt = now - lastRpmSampleMs;

    if (dt < 500)
        return;

    noInterrupts();
    uint32_t pulses = motorPgPulses;
    motorPgPulses = 0;
    interrupts();

    lastRpmSampleMs = now;

    float frequencyHz = (float(pulses) * 1000.0f) / float(dt);

    int rpm = int(lroundf(frequencyHz * 3.0f));

    if (rpm < 0)
        rpm = 0;

    if (rpm > 20000)
        rpm = 20000;

    S.RPMValue = rpm;
#endif
}

void displayTask()
{
    if (millis() - lastDisplay < DISPLAY_FAST_PERIOD_MS)
        return;

    lastDisplay = millis();

    float displayTwistError = 0.0f;

    if (S.twistReady && S.wheelIndex > 0.0f)
    {
        displayTwistError = -shortestArcPath(
            S.targetTwist,
            S.actualTwist,
            S.wheelIndex
        );
    }

    sendKV("T",   S.targetTwist);
    sendKV("E",   displayTwistError);
    sendKV("TIP", S.tipDegrees);
    sendKV("ZMM", S.zMM);
    sendKV("F",   S.forceValue);
    sendKV("RPM", S.RPMSetpoint);
    sendKV("FLW", S.flowSetpoint * S.flowTicksToMlMin);
    sendKV("MST", markTargetStatus());
    sendActiveCut();

    switch (displaySlot)
    {
        case 0:
            sendKV("RPV", S.RPMValue);
            sendActiveCut(true); // periodic refresh also recovers a rebooted display
            break;
        case 1:  sendKV("DIR",  S.motorDir); break;
        case 2:  sendKV("FLD",  S.flow_dir); break;
        case 3:  sendKV("TLK",  S.twistLock); break;
        case 4:  sendKV("ZLK",  S.zLock); break;
        case 5:  sendKV("WIDX", int(S.wheelIndex)); break;
        case 6:  sendKV("TIDX", S.twistIdx); break;
        case 7:  sendKV("ZIDX", S.zIdx); break;
        case 8:  sendKV("SSV",  S.spinServoIdx); break;
        case 9:  sendKV("MID",  S.markIdx); break;
        case 10: sendKV("MCT",  int(S.markPoints.size())); break;

        case 11:
        {
            float left = 0.0f;

            if (!S.markPoints.empty())
            {
                int count = int(S.markPoints.size());
                int li = (S.markIdx - 1 + count) % count;
                left = S.markPoints[li];
            }

            sendKV("ML", left);
            break;
        }

        case 12:
        {
            float right = 0.0f;

            if (!S.markPoints.empty())
            {
                int count = int(S.markPoints.size());
                int ri = (S.markIdx + 1) % count;
                right = S.markPoints[ri];
            }

            sendKV("MR", right);
            break;
        }

        default:
            displaySlot = 0;
            return;
    }

    displaySlot++;

    if (displaySlot > 12)
        displaySlot = 0;
}

void autoSave()
{
    if (!S.dirty)
        return;

    if (millis() - lastSave < SAVE_DEBOUNCE_MS)
        return;

    lastSave = millis();

    bool ok = saveState(S);

    Serial.print("STATE SAVE ");
    Serial.print(ok ? "OK " : "FAIL ");
    Serial.print(lastStateBytesWritten);
    Serial.print(" ");
    Serial.print(lastStateSaveBackend);
    Serial.print(" ");
    Serial.println(lastStateSavePath);
}

static const char* stateLoadResultText(StateLoadResult r)
{
    switch (r)
    {
        case STATE_LOAD_OK:           return "OK";
        case STATE_LOAD_NO_FILE:      return "NO_FILE";
        case STATE_LOAD_OPEN_FAILED:  return "OPEN_FAILED";
        case STATE_LOAD_SHORT_READ:   return "SHORT_READ";
        case STATE_LOAD_BAD_HEADER:   return "BAD_HEADER";
        case STATE_LOAD_BAD_CHECKSUM: return "BAD_CHECKSUM";
        default:                      return "UNKNOWN";
    }
}

static void forceSafeBootState()
{
    // Restore setpoints and directions from flash, but never boot motion active.
    S.twistLock = 0;
    S.zLock = 0;
    S.twistError = 0.0f;
    S.twistReady = false;

    // Pump: 0/2 are running states, 1/3 are paused in the same directions.
    if (S.flow_dir == 0)
        S.flow_dir = 1;
    else if (S.flow_dir == 2)
        S.flow_dir = 3;

    // ESC: with this hardware enable polarity, 0/2 are active, 1/3 are paused.
    if (S.motorDir == 0)
        S.motorDir = 3;
    else if (S.motorDir == 2)
        S.motorDir = 1;
}

void setup()
{
    Serial.begin(115200);

    initSteppers();
    delay(100);

    initESCMotor();

    keysSerial.begin(115200);
    dispSerial.begin(DISPLAY_BAUD);
    mastSerial.begin(115200);

    bool sdOk = beginGemSd();
    Serial.print("SD ");
    Serial.println(sdOk ? "READY" : "NO CARD");

    setStateFilesystemAvailable(false);

    bool loaded = loadState(S);

    Serial.print("STATE LOAD ");
    Serial.print(stateLoadResultText(lastStateLoadResult));
    Serial.print(" ");
    Serial.print(lastStateLoadBackend);
    Serial.print(" ");
    Serial.println(lastStateLoadPath);

    if (!loaded)
    {
        S.wheelIndex = 96.0f;
        updateWheelIndex(S, S.wheelIndex);
        resetMarkPointsToDefault(&S);

        if (!S.markPoints.empty())
        {
            S.markIdx = 0;
            S.targetTwist = S.markPoints[0];
            S.targetValid = true;
        }
        else
        {
            S.targetTwist = 0.0f;
            S.targetValid = true;
        }

        S.dirty = true;

        if (lastStateLoadResult == STATE_LOAD_NO_FILE)
        {
            bool ok = saveState(S);
            Serial.print("STATE DEFAULT SAVE ");
            Serial.print(ok ? "OK " : "FAIL ");
            Serial.print(lastStateBytesWritten);
            Serial.print(" ");
            Serial.print(lastStateSaveBackend);
            Serial.print(" ");
            Serial.println(lastStateSavePath);
        }
        else
        {
            Serial.println("STATE DEFAULT NOT SAVED");
        }
    }
    else
    {
        updateWheelIndex(S, S.wheelIndex);

        if (S.markPoints.empty())
            resetMarkPointsToDefault(&S);

        if (!S.targetValid)
        {
            if (!S.markPoints.empty())
                S.targetTwist = S.markPoints[S.markIdx];
            else
                S.targetTwist = 0.0f;

            S.targetValid = true;
        }
    }

    forceSafeBootState();

    hardStopZ();

    twistDirStep.setSpeed(0.0f);
    twistDirStep.move(0);
    twistDirStep.setCurrentPosition(0);
    setTwistDriverEnabled(false);

#if !USE_LAP_MOTOR_RS485
    pinMode(motorPGpin, INPUT);
    attachInterrupt(digitalPinToInterrupt(motorPGpin), motorPgISR, RISING);
#endif

    lastRpmSampleMs = millis();

    Serial.println("BASE READY OLD MOTION NEW COMMS");
}

void loop()
{
    mastTask();
    displayRxTask();
    keyboardTask();
    usbDiagnosticTask();

    twistWatchdogTask();
    pollDoubleClick(&S);
    rpmTask();
    displayTask();
    autoSave();
    updateMotors(S);
}
