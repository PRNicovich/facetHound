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
#include "builtinGemCuts.h"

// Integration trials: change one flag to true and upload only this base sketch
// to reverse that link's GPIO direction without reflashing the remote module.
constexpr bool KEYBOARD_UART_SWAP_TRIAL = false;
constexpr bool MAST_UART_SWAP_TRIAL = false;

SerialPIO keysSerial(KEYBOARD_UART_SWAP_TRIAL ? 6 : 7,
                     KEYBOARD_UART_SWAP_TRIAL ? 7 : 6, 128);
SerialPIO dispSerial(5, 4, 256);
SerialPIO mastSerial(MAST_UART_SWAP_TRIAL ? 2 : 3,
                     MAST_UART_SWAP_TRIAL ? 3 : 2, 256);

SystemState S;
GemSdDesign activeGemDesign;
GemRuntimeGeometry activeGemGeometry;
bool activeGemLoaded = false;
int lastJobCutSent = -1;
int builtinCutIndex = 0;
int lastBuiltinCutSent = -1;
uint8_t displayVisualMode = 0; // 0 classic/unknown, 1 dynamic, 2 static
bool displayModeKnown = false;
char activeGemCacheStatus[20] = "NONE";

static const float TWIST_ENCODER_COUNTS = 4096.0f;

static const float TIP_COUNTS_CLASSIC = 131072.0f;

static const uint32_t DISPLAY_BAUD = 460800;
// One short UART record per tick. The display-side SerialPIO FIFO is only 32
// bytes, so sending the whole screen as one burst causes silent line loss.
static const uint32_t DISPLAY_FAST_PERIOD_MS = 10;
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
uint32_t lastKeyboardRxMs = 0;
bool keyboardLinkReported = false;
uint32_t lastMastRxMs = 0;
bool mastLinkReported = false;
uint32_t lastDisplayRxMs = 0;
bool displayLinkReported = false;
uint32_t lastDisplayRoundTripMs = 0;
bool displayRoundTripReported = false;
bool displayTestMode = false;
bool uartTraceEnabled = false;
uint32_t lastDisplayLoopbackMs = 0;
uint32_t displayLoopbackUntilMs = 0;
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

            if (uartTraceEnabled)
            {
                Serial.print("@RAW,MAST,");
                Serial.println(mastBuf);
            }

            if (mastBuf[0] != '@')
                continue;

            char* comma = strchr(mastBuf, ',');
            if (!comma)
                continue;

            *comma = '\0';

            const char* key = mastBuf + 1;
            const char* valueText = comma + 1;

            float val = 0.0f;
            bool validMastRecord = false;

            if (!parseStrictFloat(valueText, &val))
                continue;

            if (!strcmp(key, "tip"))
            {
                S.tipEncoder = int(val);

                updateTipDegreesFromEncoder();

                checkServoStatus(&S);
                validMastRecord = true;
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
                validMastRecord = true;
            }
            else if (!strcmp(key, "force"))
            {
                if (val < 0.0f || val > 140000.0f)
                    continue;

                S.forceValue = val;
                validMastRecord = true;
            }

            if (validMastRecord)
            {
                lastMastRxMs = millis();
                if (!mastLinkReported)
                {
                    mastLinkReported = true;
                    Serial.println("@LINK,MAST,RX_ACTIVE");
                }
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

    // Polling is harmless with the streaming mast and also wakes the legacy
    // compatible snapshot path, giving each base-only trial a prompt response.
    static uint32_t lastMastProbeMs = 0;
    if (millis() - lastMastProbeMs >= 1000)
    {
        mastSerial.write('?');
        lastMastProbeMs = millis();
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

static bool oppositeFacetApproach(float storedTipDegrees)
{
    return storedTipDegrees < 0.0f || storedTipDegrees > 90.0f;
}

static float machineIndexForFacet(float rawIndex, float storedTipDegrees,
                                  float sourceWheelIndex)
{
    float index = rawIndex;
    if (oppositeFacetApproach(storedTipDegrees)) index += sourceWheelIndex * 0.5f;
    index = wrapPositive(index, sourceWheelIndex);
    if (S.wheelIndex > 0.0f && sourceWheelIndex > 0.0f)
        index *= S.wheelIndex / sourceWheelIndex;
    return wrapPositive(index, S.wheelIndex);
}

static void targetBuiltinCut()
{
    builtinCutIndex = constrain(builtinCutIndex, 0,
                                int(BuiltinGem::kCutCount) - 1);
    const BuiltinGem::Cut& cut = BuiltinGem::kCuts[builtinCutIndex];
    S.targetTwist = machineIndexForFacet(
        cut.rawIndex, cut.storedTipDegrees, BuiltinGem::kWheelIndex);
    S.targetValid = true;
    notifyTwistTargetChanged();
    S.dirty = true;
}

static void sendActiveCut(bool force = false)
{
    if (!activeGemLoaded)
    {
        if (builtinCutIndex < 0 || builtinCutIndex >= int(BuiltinGem::kCutCount))
            builtinCutIndex = 0;
        if (!force && lastBuiltinCutSent == builtinCutIndex) return;

        const BuiltinGem::Cut& cut = BuiltinGem::kCuts[builtinCutIndex];
        dispSerial.print("@JOB,");
        dispSerial.print(cut.tier);
        dispSerial.print(",");
        dispSerial.print(cut.facet);
        dispSerial.print(",");
        dispSerial.print(cut.storedTipDegrees, 4);
        dispSerial.print(",0.0000000,");
        dispSerial.print(cut.rawIndex, 4);
        dispSerial.println(",");
        lastBuiltinCutSent = builtinCutIndex;
        return;
    }

    if (S.markIdx < 0 || size_t(S.markIdx) >= activeGemDesign.cuts.size())
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

static void setDisplayVisualMode(const char* mode)
{
    const uint8_t next = !strcasecmp(mode, "DYNAMIC") ? 1 :
                         !strcasecmp(mode, "STATIC") ? 2 : 0;
    const bool enteringGemView = displayVisualMode == 0 && next != 0;
    displayVisualMode = next;
    displayModeKnown = true;
    if (!enteringGemView || activeGemLoaded) return;

    const uint8_t firstTier = BuiltinGem::kCuts[0].tier;
    float lowestIndex = INFINITY;
    for (uint16_t i = 0; i < BuiltinGem::kCutCount; ++i)
    {
        const BuiltinGem::Cut& cut = BuiltinGem::kCuts[i];
        if (cut.tier != firstTier) continue;
        const float index = machineIndexForFacet(
            cut.rawIndex, cut.storedTipDegrees, BuiltinGem::kWheelIndex);
        if (index < lowestIndex)
        {
            lowestIndex = index;
            builtinCutIndex = i;
        }
    }
    targetBuiltinCut();
    sendActiveCut(true);
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
        S.markPoints.push_back(machineIndexForFacet(
            cut.index, cut.angleDegrees, design.wheelIndex));

    S.markIdx = 0;
    const uint16_t firstTier = design.cuts.front().tier;
    for (size_t i = 1; i < design.cuts.size(); ++i)
        if (design.cuts[i].tier == firstTier &&
            S.markPoints[i] < S.markPoints[size_t(S.markIdx)])
            S.markIdx = int(i);
    S.targetTwist = S.markPoints[size_t(S.markIdx)];
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
        setDisplayVisualMode(value);
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

            if (uartTraceEnabled)
            {
                Serial.print("@RAW,DISPLAY,");
                Serial.println(dispBuf);
            }

            if (dispBuf[0] != '@')
                continue;

            char* comma = strchr(dispBuf, ',');
            if (!comma) continue;
            *comma = '\0';
            const char* key = dispBuf + 1;
            char* valueText = comma + 1;

            // Count raw bytes separately, but declare the link healthy only
            // after receiving a correctly framed protocol line.  This keeps a
            // floating or wrong-baud RX pin from looking like a valid link.
            lastDisplayRxMs = millis();
            if (!displayLinkReported)
            {
                displayLinkReported = true;
                Serial.println("@LINK,DISPLAY,RX_ACTIVE");
            }

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
            else if (!strcmp(key, "MODE") &&
                     (!strcasecmp(valueText, "CLASSIC") ||
                      !strcasecmp(valueText, "DYNAMIC") ||
                      !strcasecmp(valueText, "STATIC")))
            {
                setDisplayVisualMode(valueText);
                lastDisplayRoundTripMs = millis();
                if (!displayRoundTripReported)
                {
                    displayRoundTripReported = true;
                    Serial.print("@LINK,DISPLAY,ROUNDTRIP,");
                    Serial.println(valueText);
                }
            }
            else if (!strcmp(key, "PONG") && !strcmp(valueText, "BASE"))
            {
                lastDisplayRoundTripMs = millis();
                if (!displayRoundTripReported)
                {
                    displayRoundTripReported = true;
                    Serial.println("@LINK,DISPLAY,ROUNDTRIP,PONG");
                }
            }
            else if (!strcmp(key, "LOOPBACK") && !strcmp(valueText, "BASE"))
            {
                lastDisplayLoopbackMs = millis();
                Serial.println("@LINK,DISPLAY,BASE_GPIO_LOOPBACK");
            }
            else if (!strcmp(key, "HELLO") && !strcasecmp(valueText, "DISPLAY"))
            {
                // The regular base telemetry is already the display's return
                // heartbeat.  Keep HELLO side-effect free so reconnecting a
                // display cannot change machine state.
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

static void printHexByte(uint8_t value)
{
    if (value < 0x10) Serial.print('0');
    Serial.print(value, HEX);
}

static void sendUsbMotorStatus()
{
    const LapMotorDiagnostics& d = lapMotorDiagnostics();
    Serial.print("@MOTOR,lap=");
    Serial.print(!d.enabled ? "disabled" : (lapMotorLinkUp() ? "up" : "down"));
    Serial.print(",waiting="); Serial.print(d.awaitingReply ? 1 : 0);
    Serial.print(",tx="); Serial.print(d.txFrames);
    Serial.print(",rx_bytes="); Serial.print(d.rxBytes);
    Serial.print(",valid="); Serial.print(d.validReplies);
    Serial.print(",write_ack="); Serial.print(d.writeAcks);
    Serial.print(",read_reply="); Serial.print(d.readReplies);
    Serial.print(",timeout="); Serial.print(d.timeouts);
    Serial.print(",crc="); Serial.print(d.crcErrors);
    Serial.print(",exception="); Serial.print(d.exceptions);
    Serial.print(",unexpected="); Serial.print(d.unexpectedReplies);
    Serial.print(",age_ms=");
    Serial.print(d.lastValidReplyMs ? millis() - d.lastValidReplyMs : 0);
    Serial.print(",last_fn=0x"); printHexByte(d.lastFunction);
    Serial.print(",last_exception=0x"); printHexByte(d.lastException);
    Serial.print(",last=");
    if (!d.lastReplyLength)
        Serial.print("none");
    else
        for (uint8_t i = 0; i < d.lastReplyLength; ++i)
        {
            if (i) Serial.print('-');
            printHexByte(d.lastReply[i]);
        }
    Serial.println(",lap_uart=pio,tmc_uart=setup_only");
}

static void sendUsbSdStatus()
{
    const GemSdDiagnostics& d = gemSdDiagnostics();
    Serial.print("@SD,state="); Serial.print(d.ready ? "ready" : "missing");
    Serial.print(",attempts="); Serial.print(d.beginAttempts);
    Serial.print(",successes="); Serial.print(d.beginSuccesses);
    Serial.print(",attempt_age_ms=");
    Serial.print(d.beginAttempts ? millis() - d.lastAttemptMs : 0);
    Serial.print(",root=");
    Serial.print(!d.rootChecked ? "unchecked" : (d.rootReadable ? "readable" : "failed"));
    Serial.print(",files="); Serial.println(d.lastFileCount);
}

static void sendUsbSdList()
{
    const size_t count = gemSdFileCount();
    sendUsbSdStatus();
    Serial.print("@SD,listing="); Serial.println(count);
    for (size_t i = 0; i < count; ++i)
    {
        char name[GEM_SD_FILE_NAME_LENGTH] = {};
        if (!gemSdFileNameAt(i, name, sizeof(name))) continue;
        Serial.print("@SD_FILE,"); Serial.print(i); Serial.print(','); Serial.println(name);
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
    Serial.println(activeGemLoaded ? activeGemDesign.fileName : "builtin");

    Serial.print("@IO,mast_rx="); Serial.print(S.mastRxCount);
    Serial.print(",mast_link=");
    Serial.print(lastMastRxMs && millis() - lastMastRxMs < 3500 ? "up" : "down");
    Serial.print(",mast_age_ms=");
    Serial.print(lastMastRxMs ? millis() - lastMastRxMs : 0);
    Serial.print(",key_rx="); Serial.print(keyRxCount);
    Serial.print(",keyboard_link=");
    Serial.print(lastKeyboardRxMs && millis() - lastKeyboardRxMs < 2500 ? "up" : "down");
    Serial.print(",keyboard_age_ms=");
    Serial.print(lastKeyboardRxMs ? millis() - lastKeyboardRxMs : 0);
    Serial.print(",display_rx="); Serial.print(displayRxCount);
    Serial.print(",display_link=");
    Serial.print(lastDisplayRxMs && millis() - lastDisplayRxMs < 2500 ? "up" : "down");
    Serial.print(",display_age_ms=");
    Serial.print(lastDisplayRxMs ? millis() - lastDisplayRxMs : 0);
    Serial.print(",display_roundtrip=");
    Serial.print(lastDisplayRoundTripMs && millis() - lastDisplayRoundTripMs < 2500 ? "up" : "down");
    Serial.print(",display_test=");
    Serial.print(displayTestMode ? "on" : "off");
    Serial.print(",display_loopback=");
    Serial.print(lastDisplayLoopbackMs && millis() - lastDisplayLoopbackMs < 5000 ? "up" : "down");
    Serial.print(",menu=");
    Serial.print(settingsMenuOpen ? "open" : "closed");
    Serial.print(",display_mode=");
    Serial.print(!displayModeKnown ? "unknown" :
                 displayVisualMode == 1 ? "dynamic" :
                 displayVisualMode == 2 ? "static" : "classic");
    Serial.print(",rx_levels=M"); Serial.print(digitalRead(MAST_UART_SWAP_TRIAL ? 3 : 2));
    Serial.print("K"); Serial.print(digitalRead(KEYBOARD_UART_SWAP_TRIAL ? 7 : 6));
    Serial.print("D"); Serial.print(digitalRead(4));
    Serial.print(",uart_map=M"); Serial.print(MAST_UART_SWAP_TRIAL ? "swapped" : "normal");
    Serial.print("/K"); Serial.print(KEYBOARD_UART_SWAP_TRIAL ? "swapped" : "normal");
    Serial.print(",rx_overflow=M"); Serial.print(mastSerial.overflow() ? 1 : 0);
    Serial.print("K"); Serial.print(keysSerial.overflow() ? 1 : 0);
    Serial.print("D"); Serial.print(dispSerial.overflow() ? 1 : 0);
    Serial.print(",usb_rx="); Serial.println(usbRxCount);
    sendUsbMotorStatus();
    sendUsbSdStatus();
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
    Serial.println("@HELP,RPM <0..200> | MOTOR CW|CCW|OFF|STATUS|PROBE | FLOW <0..750>");
    Serial.println("@HELP,PUMP FWD|REV|OFF | STOP | HELP");
    Serial.println("@HELP,SD STATUS|RETRY|LIST | PROBE | TEST DISPLAY ON|OFF|LOOPBACK | TRACE ON|OFF");
}

static bool selectAdjacentGemTier(bool forward)
{
    const size_t count = min(activeGemDesign.cuts.size(), S.markPoints.size());
    if (!activeGemLoaded || count == 0) return false;

    size_t current = S.markIdx >= 0 ? size_t(S.markIdx) : 0;
    if (current >= count) current = 0;
    const uint16_t currentTier = activeGemDesign.cuts[current].tier;
    uint16_t targetTier = currentTier;
    bool foundTier = false;
    for (size_t i = 0; i < count; ++i)
    {
        const uint16_t tier = activeGemDesign.cuts[i].tier;
        if ((forward && tier > currentTier && (!foundTier || tier < targetTier)) ||
            (!forward && tier < currentTier && (!foundTier || tier > targetTier)))
        {
            targetTier = tier;
            foundTier = true;
        }
    }
    if (!foundTier)
    {
        targetTier = activeGemDesign.cuts[0].tier;
        for (size_t i = 1; i < count; ++i)
        {
            const uint16_t tier = activeGemDesign.cuts[i].tier;
            if ((forward && tier < targetTier) || (!forward && tier > targetTier))
                targetTier = tier;
        }
    }

    size_t selected = 0;
    float lowestIndex = INFINITY;
    for (size_t i = 0; i < count; ++i)
    {
        if (activeGemDesign.cuts[i].tier != targetTier) continue;
        if (S.markPoints[i] < lowestIndex)
        {
            lowestIndex = S.markPoints[i];
            selected = i;
        }
    }
    S.markIdx = int(selected);
    homeMarkPoint(&S);
    sendActiveCut(true);
    return true;
}

static bool selectAdjacentGemFacet(bool forward)
{
    const size_t count = min(activeGemDesign.cuts.size(), S.markPoints.size());
    if (!activeGemLoaded || count == 0) return false;
    size_t current = S.markIdx >= 0 ? size_t(S.markIdx) : 0;
    if (current >= count) current = 0;
    const uint16_t tier = activeGemDesign.cuts[current].tier;
    const float currentIndex = S.markPoints[current];

    size_t selected = current;
    float selectedIndex = forward ? INFINITY : -INFINITY;
    bool found = false;
    for (size_t i = 0; i < count; ++i)
    {
        if (activeGemDesign.cuts[i].tier != tier || i == current) continue;
        const float index = S.markPoints[i];
        if ((forward && index > currentIndex + 0.0001f && index < selectedIndex) ||
            (!forward && index < currentIndex - 0.0001f && index > selectedIndex))
        {
            selected = i;
            selectedIndex = index;
            found = true;
        }
    }
    if (!found)
    {
        selectedIndex = forward ? INFINITY : -INFINITY;
        for (size_t i = 0; i < count; ++i)
        {
            if (activeGemDesign.cuts[i].tier != tier) continue;
            const float index = S.markPoints[i];
            if ((forward && index < selectedIndex) || (!forward && index > selectedIndex))
            {
                selected = i;
                selectedIndex = index;
            }
        }
    }
    S.markIdx = int(selected);
    homeMarkPoint(&S);
    sendActiveCut(true);
    return true;
}

static bool selectAdjacentBuiltin(bool changeTier, bool forward)
{
    const BuiltinGem::Cut& current = BuiltinGem::kCuts[builtinCutIndex];
    uint8_t targetTier = current.tier;
    if (changeTier)
    {
        bool found = false;
        for (uint16_t i = 0; i < BuiltinGem::kCutCount; ++i)
        {
            const uint8_t tier = BuiltinGem::kCuts[i].tier;
            if ((forward && tier > current.tier && (!found || tier < targetTier)) ||
                (!forward && tier < current.tier && (!found || tier > targetTier)))
            {
                targetTier = tier;
                found = true;
            }
        }
        if (!found) targetTier = forward ? BuiltinGem::kCuts[0].tier
                                         : BuiltinGem::kCuts[BuiltinGem::kCutCount - 1].tier;
    }

    const float currentIndex = machineIndexForFacet(
        current.rawIndex, current.storedTipDegrees, BuiltinGem::kWheelIndex);
    int selected = builtinCutIndex;
    float selectedIndex = forward && !changeTier ? INFINITY :
                          !forward && !changeTier ? -INFINITY : INFINITY;
    bool found = false;
    for (uint16_t i = 0; i < BuiltinGem::kCutCount; ++i)
    {
        const BuiltinGem::Cut& candidate = BuiltinGem::kCuts[i];
        if (candidate.tier != targetTier || (!changeTier && i == builtinCutIndex)) continue;
        const float index = machineIndexForFacet(
            candidate.rawIndex, candidate.storedTipDegrees, BuiltinGem::kWheelIndex);
        const bool acceptable = changeTier ||
            (forward ? index > currentIndex + 0.0001f : index < currentIndex - 0.0001f);
        if (!acceptable) continue;
        if (!found || (changeTier ? index < selectedIndex
                                  : (forward ? index < selectedIndex
                                             : index > selectedIndex)))
        {
            selected = i;
            selectedIndex = index;
            found = true;
        }
    }
    if (!found && !changeTier)
    {
        selectedIndex = forward ? INFINITY : -INFINITY;
        for (uint16_t i = 0; i < BuiltinGem::kCutCount; ++i)
        {
            const BuiltinGem::Cut& candidate = BuiltinGem::kCuts[i];
            if (candidate.tier != targetTier) continue;
            const float index = machineIndexForFacet(
                candidate.rawIndex, candidate.storedTipDegrees, BuiltinGem::kWheelIndex);
            if ((forward && index < selectedIndex) || (!forward && index > selectedIndex))
            {
                selected = i;
                selectedIndex = index;
            }
        }
    }

    builtinCutIndex = selected;
    targetBuiltinCut();
    sendActiveCut(true);
    return true;
}

static void routeKeyboardKey(uint8_t key)
{
    if (key == MENU_TOGGLE_KEY)
    {
        // The display is the menu-state authority. Optimistically suppress
        // motion until its immediate/heartbeat reply confirms OPEN or CLOSED.
        settingsMenuOpen = true;
        sendDisplayLine("@MENU,TOGGLE");
    }
    else if (settingsMenuOpen)
    {
        if (key == MENU_DELETE_KEY)
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
        const bool useBuiltinGem = !activeGemLoaded && displayVisualMode != 0;
        if (key == 5)
        {
            if (activeGemLoaded) selectAdjacentGemTier(true);
            else if (useBuiltinGem) selectAdjacentBuiltin(true, true);
        }
        else if (key == 6)
        {
            if (activeGemLoaded) selectAdjacentGemTier(false);
            else if (useBuiltinGem) selectAdjacentBuiltin(true, false);
        }
        else if (key == 12)
        {
            if (activeGemLoaded) selectAdjacentGemFacet(true);
            else if (useBuiltinGem) selectAdjacentBuiltin(false, true);
            else handleKey(&S, key);
        }
        else if (key == 13)
        {
            if (activeGemLoaded) selectAdjacentGemFacet(false);
            else if (useBuiltinGem) selectAdjacentBuiltin(false, false);
            else handleKey(&S, key);
        }
        else if (key == 14 && useBuiltinGem)
        {
            targetBuiltinCut();
            sendActiveCut(true);
        }
        else
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
    if (!strcasecmp(command, "PROBE"))
    {
        mastSerial.write('?');
        sendDisplayLine("@MODE,?");
        requestLapMotorProbe();
        Serial.println("@ACK,PROBE,MAST_DISPLAY_AND_LAP");
        return;
    }
    if (!strcasecmp(command, "TRACE"))
    {
        char* mode = strtok_r(nullptr, " \t", &save);
        if (mode && !strcasecmp(mode, "ON"))
        {
            uartTraceEnabled = true;
            Serial.println("@ACK,TRACE,ON");
        }
        else if (mode && !strcasecmp(mode, "OFF"))
        {
            uartTraceEnabled = false;
            Serial.println("@ACK,TRACE,OFF");
        }
        else
        {
            Serial.println("@ERR,TRACE,expected ON|OFF");
        }
        return;
    }
    if (!strcasecmp(command, "TEST"))
    {
        char* target = strtok_r(nullptr, " \t", &save);
        char* mode = strtok_r(nullptr, " \t", &save);
        if (!target || strcasecmp(target, "DISPLAY") || !mode)
        {
            Serial.println("@ERR,TEST,expected DISPLAY ON|OFF|LOOPBACK");
            return;
        }
        if (!strcasecmp(mode, "ON"))
        {
            displayTestMode = true;
            Serial.println("@ACK,TEST,DISPLAY,ON");
        }
        else if (!strcasecmp(mode, "OFF"))
        {
            displayTestMode = false;
            sendActiveCut(true);
            Serial.println("@ACK,TEST,DISPLAY,OFF");
        }
        else if (!strcasecmp(mode, "LOOPBACK"))
        {
            displayTestMode = false;
            lastDisplayLoopbackMs = 0;
            displayLoopbackUntilMs = millis() + 5000;
            Serial.println("@ACK,TEST,DISPLAY,LOOPBACK,5_SECONDS");
        }
        else
        {
            Serial.println("@ERR,TEST,expected DISPLAY ON|OFF|LOOPBACK");
        }
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
    if (!strcasecmp(command, "SD"))
    {
        char* mode = strtok_r(nullptr, " \t", &save);
        if (!mode || !strcasecmp(mode, "STATUS"))
        {
            sendUsbSdStatus();
        }
        else if (!strcasecmp(mode, "RETRY"))
        {
            const bool ready = retryGemSd();
            Serial.print("@ACK,SD,RETRY,"); Serial.println(ready ? "READY" : "MISSING");
            if (ready) gemSdFileCount();
            sendUsbSdStatus();
        }
        else if (!strcasecmp(mode, "LIST"))
        {
            sendUsbSdList();
        }
        else
        {
            Serial.println("@ERR,SD,expected STATUS|RETRY|LIST");
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
            else if (!strcasecmp(mode, "STATUS"))
            {
                sendUsbMotorStatus();
                return;
            }
            else if (!strcasecmp(mode, "PROBE"))
            {
                requestLapMotorProbe();
                Serial.println("@ACK,MOTOR,PROBE");
                return;
            }
            else { Serial.println("@ERR,MOTOR,expected CW|CCW|OFF|STATUS|PROBE"); return; }
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

            if (uartTraceEnabled)
            {
                Serial.print("@RAW,KEYBOARD,");
                Serial.println(keyBuf);
            }

            bool validKeyboardRecord = false;
            if (!strcmp(keyBuf, "@HELLO,KEYBOARD"))
            {
                validKeyboardRecord = true;
            }
            else if (keyBuf[0] == 'D' && keyBuf[1] == ',')
            {
                long key = -1;
                if (parseStrictLong(&keyBuf[2], &key) && key >= 0 && key <= 255)
                {
                    Serial.print("@KEY,"); Serial.println(key);
                    routeKeyboardKey(uint8_t(key));
                    validKeyboardRecord = true;
                }
            }

            if (validKeyboardRecord)
            {
                lastKeyboardRxMs = millis();
                if (!keyboardLinkReported)
                {
                    keyboardLinkReported = true;
                    Serial.println("@LINK,KEYBOARD,RX_ACTIVE");
                }
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
    const uint32_t now = millis();

    if (displayLoopbackUntilMs && int32_t(displayLoopbackUntilMs - now) > 0)
    {
        static uint32_t lastLoopbackProbeMs = 0;
        if (now - lastLoopbackProbeMs >= 250)
        {
            sendDisplayLine("@LOOPBACK,BASE");
            lastLoopbackProbeMs = now;
        }
        return;
    }
    displayLoopbackUntilMs = 0;

    static uint32_t lastDisplayProbeMs = 0;
    if (now - lastDisplayProbeMs >= 500)
    {
        sendDisplayLine(displayModeKnown ? "@PING,BASE" : "@MODE,?");
        lastDisplayProbeMs = now;
    }

    // Menu state is a safety interlock, so repair it continuously instead of
    // trusting one OPEN/CLOSED packet forever.
    static uint32_t lastMenuProbeMs = 0;
    if (now - lastMenuProbeMs >= 1000)
    {
        sendDisplayLine("@MENU,?");
        lastMenuProbeMs = now;
    }

    // Establish a genuine request/reply before starting screen telemetry. This
    // keeps an unready or one-way display link completely free of data bursts.
    if (!displayTestMode &&
        (!lastDisplayRoundTripMs || now - lastDisplayRoundTripMs >= 2500))
        return;

    if (now - lastDisplay < DISPLAY_FAST_PERIOD_MS)
        return;

    lastDisplay = now;

    if (displayTestMode)
    {
        const float phase = TWO_PI * float(now % 8000UL) / 8000.0f;
        const float indexError = 12.0f * sinf(phase);

        // Exactly one record per call prevents overflowing the display's
        // 32-byte PIO UART queue while it is busy rendering a frame.
        switch (displaySlot)
        {
            case 0:  sendKV("T", 24.0f); break;
            case 1:  sendKV("E", indexError); break;
            case 2:  sendKV("TIP", 45.0f + 12.0f * sinf(phase * 0.5f)); break;
            case 3:  sendKV("ZMM", 100.0f + 25.0f * cosf(phase)); break;
            case 4:  sendKV("F", 0.0f); break;
            case 5:  sendKV("RPM", 120); break;
            case 6:  sendKV("RPV", 118 + int(3.0f * sinf(phase * 7.0f))); break;
            case 7:  sendKV("FLW", 25.0f + 2.0f * sinf(phase * 5.0f)); break;
            case 8:  sendKV("DIR", 1); break;
            case 9:  sendKV("FLD", 2); break;
            case 10: sendKV("WIDX", 96); break;
            default: displaySlot = 0; return;
        }
        displaySlot = (displaySlot + 1) % 11;
        return;
    }

    float displayTwistError = 0.0f;

    if (S.twistReady && S.wheelIndex > 0.0f)
    {
        displayTwistError = -shortestArcPath(
            S.targetTwist,
            S.actualTwist,
            S.wheelIndex
        );
    }

    switch (displaySlot)
    {
        case 0:  sendKV("T",   S.targetTwist); break;
        case 1:  sendKV("E",   displayTwistError); break;
        case 2:  sendKV("TIP", S.tipDegrees); break;
        case 3:  sendKV("ZMM", S.zMM); break;
        case 4:  sendKV("F",   S.forceValue); break;
        case 5:  sendKV("RPM", S.RPMSetpoint); break;
        case 6:  sendKV("FLW", S.flowSetpoint * S.flowTicksToMlMin); break;
        case 7:  sendKV("MST", markTargetStatus()); break;
        case 8:  sendActiveCut(); break;
        case 9:  sendKV("RPV", S.RPMValue); break;
        case 10: sendKV("DIR",  S.motorDir); break;
        case 11: sendKV("FLD",  S.flow_dir); break;
        case 12: sendKV("TLK",  S.twistLock); break;
        case 13: sendKV("ZLK",  S.zLock); break;
        case 14: sendKV("WIDX", int(S.wheelIndex)); break;
        case 15: sendKV("TIDX", S.twistIdx); break;
        case 16: sendKV("ZIDX", S.zIdx); break;
        case 17: sendKV("SSV",  S.spinServoIdx); break;
        case 18: sendKV("MID",  S.markIdx); break;
        case 19: sendKV("MCT",  int(S.markPoints.size())); break;

        case 20:
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

        case 21:
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

    if (displaySlot > 21)
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

    // Lap motor: 1/3 run CW/CCW; 2/0 are the matching paused states.
    if (S.motorDir == 1)
        S.motorDir = 2;
    else if (S.motorDir == 3)
        S.motorDir = 0;
}

void setup()
{
    Serial.begin(115200);

    initSteppers();
    delay(100);

    // Preserve the proven hardware power-up sequence.  On the first cold boot
    // the Pico can configure the TMC2208/2209s before their motor supply is
    // stable.  The original controller deliberately restarted once after that
    // attempt; the second boot then programs microstepping and current into
    // already-powered drivers.  A software restart is not a power-on reset, so
    // this executes only once per cold start.
    if (rp2040.getResetReason() == 1)
        rp2040.restart();

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
    Serial.print("KEYBOARD UART MAP: ");
    Serial.println(KEYBOARD_UART_SWAP_TRIAL ? "SWAPPED base TX6/RX7" : "NORMAL base TX7/RX6");
    Serial.print("MAST UART MAP: ");
    Serial.println(MAST_UART_SWAP_TRIAL ? "SWAPPED base TX2/RX3" : "NORMAL base TX3/RX2");
    Serial.println("DISPLAY UART: base TX5 -> display RX9; display TX8 -> base RX4; 460800 baud");
    Serial.println("Send STATUS or STREAM ON 500 to inspect link counters");
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
