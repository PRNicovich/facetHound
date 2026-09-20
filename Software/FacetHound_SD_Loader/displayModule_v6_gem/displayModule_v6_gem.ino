// displayModule_v6_gem.ino
// Plain @KEY,value display receiver.
// Incoming values only redraw when their displayed text would change.

#include <Arduino.h>
#include <EEPROM.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <SerialPIO.h>
#include <ctype.h>
#include <math.h>
#include <strings.h>
#include "pio_encoder.h"
#include "hardware/gpio.h"

#include "display_mode.h"
#include "gem_data.h"
#include "gem_ui.h"
#include "runtime_gem_mesh.h"
#include "settings_menu.h"

#include "diamonds.h"
#include "dcTerminal_30.h"
#include "dcTerminal_40.h"
#include "dcTerminal_60.h"
#include "dcTerminal_80.h"

#define GEM_ICON       diamonds
#define NUMBERS        dcTerminal_80
#define LOGO           dcTerminal_60
#define NUMBERS_SMALL  dcTerminal_60
#define LABELS         dcTerminal_30
#define ERRORTEXT      dcTerminal_40
#define SPRITE_FILL    0x0000

#define TXPin 8
#define RXPin 9

// Rendering can keep the main loop busy long enough to overflow SerialPIO's
// 32-byte default queue. Keep a full telemetry burst available until rxUpdate
// gets CPU time.
SerialPIO baseSerial(TXPin, RXPin, 512);
PioEncoder encoder(10);
TFT_eSPI tft = TFT_eSPI();
GemUi gemUi(tft);
SettingsMenu settingsMenu(tft);

TFT_eSprite stext1  = TFT_eSprite(&tft);
TFT_eSprite stext2  = TFT_eSprite(&tft);
TFT_eSprite stext3  = TFT_eSprite(&tft);
TFT_eSprite stext4  = TFT_eSprite(&tft);
TFT_eSprite stext5  = TFT_eSprite(&tft);
TFT_eSprite stext6  = TFT_eSprite(&tft);
TFT_eSprite stext7  = TFT_eSprite(&tft);
TFT_eSprite stext8  = TFT_eSprite(&tft);
TFT_eSprite stext9  = TFT_eSprite(&tft);
TFT_eSprite stext10 = TFT_eSprite(&tft);
TFT_eSprite stext11 = TFT_eSprite(&tft);
TFT_eSprite stext12 = TFT_eSprite(&tft);
TFT_eSprite stext13 = TFT_eSprite(&tft);
TFT_eSprite stext14 = TFT_eSprite(&tft);
TFT_eSprite stext15 = TFT_eSprite(&tft);
TFT_eSprite sBar1   = TFT_eSprite(&tft);
TFT_eSprite tierError = TFT_eSprite(&tft);
TFT_eSprite markSprite = TFT_eSprite(&tft);
TFT_eSprite markStatusSprite = TFT_eSprite(&tft);
TFT_eSprite axisLabels = TFT_eSprite(&tft);
TFT_eSprite unitLabels = TFT_eSprite(&tft);
TFT_eSprite downLabels = TFT_eSprite(&tft);

// ================= DISPLAY STATE =================

bool updateTiltAngle = true;
bool updateTiltError = true;
float tiltSetAngle = 0.0f;
float tiltSetError = 0.0f;
float actualIndexValue = 0.0f;
bool actualIndexReceived = false;
int tiltLock = 0;
bool updateTiltLockBool = true;
bool updateTiltStepIndexBool = true;
int tiltStepIndex = 1;

float markL = 0.0f;
float markR = 0.0f;
int markIdx = 0;
int nMarkIdx = 0;
int markState = 0;
bool updateMarkPointsBool = true;
bool updateMarkStatusBool = true;

int spinServoIdx = 0;
bool updateSpinServoIdxBool = true;

bool updateTipAngle = true;
float tipAngle = 0.0f;
uint32_t tipSampleSequence=0;
float nominalIndexValue=0,cheatIndexValue=0;
bool cheatReceived=false,cheatTemporary=false;
uint8_t hudFaults=0;

bool updateZValue = true;
float zValue = 0.0f;
int zedLock = 0;
bool updateZedLockBool = true;
int zedStepIndex = 0;
bool updateZedStepIndexBool = true;

bool updateRPMValue = true;
bool updateRPMsetValueBool = true;
long rpmSetValue = 0;
uint32_t RPMValue = 0;
int RPM_dir = 1;
bool updateRPMDirBool = true;

bool updateFlowRate = true;
float flowRate = 0.0f;
int Flow_dir = 1;
bool updateFlowDirBool = true;

bool updateForceBarBool = true;
int forceBar = 0;
int fillBarWidth = 0;

int32_t encCount = 0;
int32_t lastEnc = 0;
uint32_t lastEncoderTxMs = 0;
float wheelIndex = 96.0f;
bool updateWheelIndexBool = true;

bool jobActive = false;
char jobTitle[48] = {};
char jobFacetName[12] = {};
char tierComment[97] = {};
uint16_t jobTier = 0;
uint16_t jobFacet = 0;
float jobAngle = 0.0f;
float jobGemcadDistance = 0.0f;
float jobIndex = 0.0f;

char instrumentRxBuffer[160];
uint8_t instrumentRxIndex = 0;
char usbRxBuffer[160];
uint8_t usbRxIndex = 0;
bool systemReady = false;
uint32_t lastRx = 0;
bool linkAlive = false;
uint32_t telemetryVersion = 0;
bool meshReceiving = false;
bool meshDisplayFailed = false;
uint32_t meshLastRxMs = 0;
uint32_t meshExpectedRows = 0, meshCompletedRows = 0;
uint16_t meshNextVertex = 0, meshNextEdge = 0, meshNextPlane = 0;
bool bootModelPending = true;
bool loadingDesign = false;
bool splashActive = false;
static void sendLineBoth(const char* line);
char loadingTitle[64] = {};
char loadingStage[40] = "Waiting for base";
TFT_eSprite loadingProgress(&tft);

static void drawLoadingProgress(int percent, bool force = false)
{
  static int previous = -2;
  static uint32_t lastDraw = 0;
  if (!force && millis() - lastDraw < 100 && percent != 100) return;
  if (!force && percent == previous && percent >= 0) return;
  previous = percent; lastDraw = millis();
  const int y = 382;
  if (!splashActive && force) {
    tft.fillScreen(TFT_BLACK);
    tft.loadFont(ERRORTEXT);
    tft.setTextDatum(TL_DATUM);
    const int width=tft.textWidth("FACET HOUND"), x=(320-width)/2;
    const uint16_t accent=tft.color565(255,65,195), title=tft.color565(60,230,255);
    tft.setTextColor(accent,TFT_BLACK); tft.drawString("FACET HOUND",x+1,157);
    tft.setTextColor(title,TFT_BLACK); tft.drawString("FACET HOUND",x,154);
    tft.drawFastHLine(x,184,width,accent);
    tft.fillCircle(x-6,184,2,title); tft.fillCircle(x+width+6,184,2,title);
    tft.unloadFont();
  }
  loadingProgress.setColorDepth(8);
  if (!loadingProgress.createSprite(320,70)) return;
  loadingProgress.fillSprite(TFT_BLACK);
  loadingProgress.setTextDatum(MC_DATUM);
  loadingProgress.setTextColor(TFT_WHITE, TFT_BLACK);
  loadingProgress.drawString(loadingTitle, 160, 9, 2);
  loadingProgress.setTextColor(TFT_CYAN, TFT_BLACK);
  char label[64];
  if (percent >= 0) snprintf(label, sizeof(label), "%s %d%%", loadingStage, percent);
  else snprintf(label, sizeof(label), "%s...", loadingStage);
  loadingProgress.drawString(label, 160, 29, 2);
  loadingProgress.drawRect(20, 46, 280, 12, TFT_DARKGREY);
  const int width = percent < 0 ? 40 : constrain(percent, 0, 100) * 276 / 100;
  const int x = percent < 0 ? int((millis() / 8) % 236) : 0;
  loadingProgress.fillRect(22 + x, 48, width, 8, TFT_CYAN);
  loadingProgress.pushSprite(0,y);
}

static void acknowledgeMeshRow(char kind, uint16_t id)
{
  if (!splashActive) drawLoadingProgress(meshExpectedRows ?
      int(100UL * meshCompletedRows / meshExpectedRows) : 0);
  char ack[40]; snprintf(ack, sizeof(ack), "@MESHACK,%c,%u", kind, unsigned(id));
  sendLineBoth(ack);
}
DisplayMode displayMode = DisplayMode::CLASSIC;
bool restoreDisplayPending = false;
bool usbDemoMode = false;

static const uint32_t DISPLAY_BAUD = 460800;
// Drain ordinary telemetry accumulated during a full model redraw before
// painting again. All records retain wire order; none are prioritized/dropped.
static const uint16_t RX_BYTE_BUDGET = 2048;
static const uint8_t RX_LINE_BUDGET = 128;
static const uint32_t SETTINGS_MAGIC = 0x46484D36; // "FHM6"
static const char DISPLAY_FIRMWARE_ID[] = "INTEGRATION-RELEASE-20260918";

struct PersistedSettings
{
  uint32_t magic;
  uint8_t mode;
  uint8_t reserved[3];
};

// ================= HELPERS =================

static int clampInt(int v, int lo, int hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static bool parsePlainFloat(const char* s, float* out)
{
  if (!s || !out) return false;

  char* endptr = nullptr;
  float v = strtof(s, &endptr);

  if (endptr == s || !isfinite(v)) return false;

  // Accept normal newline-trimmed float text.
  while (*endptr == ' ' || *endptr == '\t') endptr++;

  if (*endptr != '\0') return false;

  *out = v;
  return true;
}

static DisplayMode loadDisplayMode()
{
  PersistedSettings settings = {};
  EEPROM.get(0, settings);

  if (settings.magic != SETTINGS_MAGIC ||
      settings.mode > static_cast<uint8_t>(DisplayMode::CLASSIC_TIER))
  {
    return DisplayMode::CLASSIC;
  }

  return static_cast<DisplayMode>(settings.mode);
}

static bool parseDisplayMode(const char* text, DisplayMode* out)
{
  if (!text || !out) return false;

  char value[16];
  size_t length = strlen(text);
  if (length >= sizeof(value)) return false;

  for (size_t i = 0; i <= length; ++i)
    value[i] = char(toupper(static_cast<unsigned char>(text[i])));

  if (!strcmp(value, "0") || !strcmp(value, "CLASSIC"))
    *out = DisplayMode::CLASSIC;
  else if (!strcmp(value, "1") || !strcmp(value, "DYNAMIC"))
    *out = DisplayMode::DYNAMIC;
  else if (!strcmp(value, "2") || !strcmp(value, "STATIC"))
    *out = DisplayMode::STATIC;
  else if (!strcmp(value, "3") || !strcmp(value, "CLASSIC_TIER"))
    *out = DisplayMode::CLASSIC_TIER;
  else
    return false;

  return true;
}

static void reportDisplayMode(const char* prefix)
{
  Serial.print(prefix);
  Serial.println(displayModeName(displayMode));
  baseSerial.print(prefix);
  baseSerial.println(displayModeName(displayMode));
}

static void sendLineBoth(const char* line)
{
  Serial.println(line);
  baseSerial.println(line);
}

static void saveDisplayModeAndRestart(DisplayMode nextMode);

static bool parseMenuKey(const char* text, MenuKey* out)
{
  if (!text || !out) return false;

  char value[16];
  size_t length = strlen(text);
  if (length >= sizeof(value)) return false;

  for (size_t i = 0; i <= length; ++i)
    value[i] = char(toupper(static_cast<unsigned char>(text[i])));

  if (!strcmp(value, "UP")) *out = MenuKey::UP;
  else if (!strcmp(value, "DOWN")) *out = MenuKey::DOWN;
  else if (!strcmp(value, "SELECT") || !strcmp(value, "OK"))
    *out = MenuKey::SELECT;
  else if (!strcmp(value, "BACK") || !strcmp(value, "ESC"))
    *out = MenuKey::BACK;
  else if (!strcmp(value, "FINER")) *out = MenuKey::FINER;
  else if (!strcmp(value, "COARSER")) *out = MenuKey::COARSER;
  else if (!strcmp(value, "DELETE")) *out = MenuKey::DELETE_ITEM;
  else return false;

  return true;
}

static void openSettingsMenu()
{
  if (settingsMenu.isOpen()) return;
  settingsMenu.open(displayMode);
  sendLineBoth("@MENU,OPEN");
  // One snapshot request on entry. Future base settings are returned as CFG rows.
  sendLineBoth("@CFGGET,ALL");
}

static void closeSettingsMenu()
{
  if (!settingsMenu.isOpen()) return;
  sendLineBoth("@CFGACTION,INDEX_SPIN_STOP,0");
  settingsMenu.close();
  restoreDisplayPending = true;
  sendLineBoth("@MENU,CLOSED");
}

static void handleSettingsKey(MenuKey key)
{
  if (!settingsMenu.isOpen()) return;
  MenuResult result = settingsMenu.handle(key);

  if (result.commandReady)
    sendLineBoth(result.command);

  if (result.closed)
  {
    restoreDisplayPending = true;
    sendLineBoth("@MENU,CLOSED");
    return;
  }

  if (result.modeSelected)
  {
    char line[48];
    snprintf(line, sizeof(line), "@CFGSET,DISPLAY_MODE,%s",
             displayModeName(result.selectedMode));
    sendLineBoth(line);
    saveDisplayModeAndRestart(result.selectedMode);
  }
}

static void saveDisplayModeAndRestart(DisplayMode nextMode)
{
  if (nextMode == displayMode)
  {
    reportDisplayMode("@MODE,");
    return;
  }

  PersistedSettings settings = {};
  settings.magic = SETTINGS_MAGIC;
  settings.mode = static_cast<uint8_t>(nextMode);
  EEPROM.put(0, settings);
  EEPROM.commit();

  {
    // Release only view buffers, never the resident geometry/cache.
    TFT_eSprite* classicSprites[] = {&stext1,&stext2,&stext3,&stext4,&stext5,
      &stext6,&stext7,&stext8,&stext9,&stext10,&stext11,&stext12,&stext13,
      &stext14,&stext15,&sBar1,&markSprite,&markStatusSprite,&axisLabels,
      &unitLabels,&downLabels,&tierError};
    for (auto* sprite : classicSprites) sprite->deleteSprite();
    if (isClassicView(nextMode)) gemUi.releaseSprites();
    // Both views share the same resident mesh and sprite dimensions.
    displayMode = nextMode;
    closeSettingsMenu();
    if (!isClassicView(displayMode)) gemUi.begin(displayMode);
    restoreDisplayPending = true;
    ++telemetryVersion;
    reportDisplayMode("@MODE,");
    // A Classic-only boot deliberately did not fetch geometry. Request it
    // once on entering a tier-aware view; ordinary mode switches reuse RAM.
    if (nextMode != DisplayMode::CLASSIC && !runtimeGemMesh().active())
      sendLineBoth("@CFGGET,MESH");
    return;
  }

}

static bool displayedFloatChanged(float oldValue, float newValue, float scale)
{
  if (!isfinite(oldValue) || !isfinite(newValue)) return true;
  return lroundf(oldValue * scale) != lroundf(newValue * scale);
}

static bool displayedIntChanged(int oldValue, int newValue)
{
  return oldValue != newValue;
}

static bool displayedLongChanged(long oldValue, long newValue)
{
  return oldValue != newValue;
}

static int stepIconLevel(int idx)
{
  switch (idx)
  {
    case 0: return 3;
    case 1: return 2;
    case 2: return 1;
  }

  return 1;
}

static void drawStepIcon(TFT_eSprite& sprite, int idx, uint16_t color)
{
  int level = stepIconLevel(idx);
  int baseY = 23;
  int startX = 8;

  for (int i = 0; i < 3; i++)
  {
    int h = 6 + (i * 5);
    int x = startX + (i * 5);
    uint16_t barColor = (i < level) ? color : SPRITE_FILL;
    sprite.fillRect(x, baseY - h, 3, h, barColor);
  }
}

static const char* markStateLabel()
{
  switch (markState)
  {
    case 0: return " ";
    case 1: return "-";
    case 2: return "*";
  }

  return "?";
}

// ================= SETTERS =================

static void setTilt(float v)
{
  if (!displayedFloatChanged(tiltSetAngle, v, 100.0f)) return;
  tiltSetAngle = v;
  updateTiltAngle = true;
}

static void setTiltError(float v)
{
  if (!displayedFloatChanged(tiltSetError, v, 100.0f)) return;
  tiltSetError = v;
  updateTiltError = true;
}

static void setTip(float v)
{
  if (!displayedFloatChanged(tipAngle, v, 100.0f)) return;
  tipAngle = v;
  updateTipAngle = true;
}

static void setZ(float v)
{
  if (!displayedFloatChanged(zValue, v, 1000.0f)) return;
  zValue = v;
  updateZValue = true;
}

static void setRPMActual(float v)
{
  uint32_t next = (uint32_t)max(0, (int)lroundf(v));
  if (RPMValue == next) return;
  RPMValue = next;
  updateRPMValue = true;
}

static void setRPMSet(float v)
{
  long next = max(0, (int)lroundf(v));
  if (!displayedLongChanged(rpmSetValue, next)) return;
  rpmSetValue = next;
  updateRPMsetValueBool = true;
  updateRPMValue = true;
}

static void setRPMDir(float v)
{
  int next = clampInt((int)lroundf(v), 0, 3);
  if (!displayedIntChanged(RPM_dir, next)) return;
  RPM_dir = next;
  updateRPMDirBool = true;
  updateRPMValue = true;
}

static void setFlow(float v)
{
  if (!displayedFloatChanged(flowRate, v, 10.0f)) return;
  flowRate = v;
  updateFlowRate = true;
}

static void setFlowDir(float v)
{
  int next = clampInt((int)lroundf(v), 0, 3);
  if (!displayedIntChanged(Flow_dir, next)) return;
  Flow_dir = next;
  updateFlowDirBool = true;
  Serial.print("@UIFLOW,dir="); Serial.print(Flow_dir);
  Serial.print(",running="); Serial.println(Flow_dir == 0 || Flow_dir == 2);
  baseSerial.print("@UIFLOW,dir="); baseSerial.print(Flow_dir);
  baseSerial.print(",running="); baseSerial.println(Flow_dir == 0 || Flow_dir == 2);
}

static void setForceRaw(float v)
{
  int next = clampInt((int)lroundf((v * 20.0f) / 25600.0f), 0, 20);
  if (!displayedIntChanged(forceBar, next)) return;
  forceBar = next;
  updateForceBarBool = true;
}

static void setForceBar(float v)
{
  int next = clampInt((int)lroundf(v), 0, 20);
  if (!displayedIntChanged(forceBar, next)) return;
  forceBar = next;
  updateForceBarBool = true;
}

static void setWheelIndex(float v)
{
  if ((int)wheelIndex == (int)v) return;
  wheelIndex = v;
  updateWheelIndexBool = true;
}

static void setTiltLock(float v)
{
  int next = ((int)lroundf(v) != 0) ? 1 : 0;
  if (!displayedIntChanged(tiltLock, next)) return;
  tiltLock = next;
  updateTiltLockBool = true;
}

static void setZLock(float v)
{
  int next = ((int)lroundf(v) != 0) ? 1 : 0;
  if (!displayedIntChanged(zedLock, next)) return;
  zedLock = next;
  updateZedLockBool = true;
}

static void setTiltStepIndex(float v)
{
  int next = clampInt((int)lroundf(v), 0, 2);
  if (!displayedIntChanged(tiltStepIndex, next)) return;
  tiltStepIndex = next;
  updateTiltStepIndexBool = true;
}

static void setZedStepIndex(float v)
{
  int next = clampInt((int)lroundf(v), 0, 2);
  if (!displayedIntChanged(zedStepIndex, next)) return;
  zedStepIndex = next;
  updateZedStepIndexBool = true;
}

static void setMarkState(float v)
{
  int next = clampInt((int)lroundf(v), 0, 2);
  if (!displayedIntChanged(markState, next)) return;
  markState = next;
  updateMarkStatusBool = true;
}

static void setSpinServo(float v)
{
  int next = ((int)lroundf(v) != 0) ? 1 : 0;
  if (!displayedIntChanged(spinServoIdx, next)) return;
  spinServoIdx = next;
  updateSpinServoIdxBool = true;
}

static void setMarkIdx(float v)
{
  int next = max(0, (int)lroundf(v));
  if (!displayedIntChanged(markIdx, next)) return;
  markIdx = next;
  updateMarkPointsBool = true;
}

static void setMarkCount(float v)
{
  int next = max(0, (int)lroundf(v));
  if (!displayedIntChanged(nMarkIdx, next)) return;
  nMarkIdx = next;
  updateMarkPointsBool = true;
}

static void setMarkLeft(float v)
{
  if (!displayedFloatChanged(markL, v, 100.0f)) return;
  markL = v;
  updateMarkPointsBool = true;
}

static void setMarkRight(float v)
{
  if (!displayedFloatChanged(markR, v, 100.0f)) return;
  markR = v;
  updateMarkPointsBool = true;
}

// ================= SERIAL RX =================

void parseLine(char* line)
{
  if (!line || line[0] == 0) return;
  if (line[0] != '@') return;

  char* comma = strchr(line, ',');
  if (!comma) return;

  *comma = '\0';

  const char* key = line + 1;
  char* valueText = comma + 1;

  if (key[0] == 0) return;
  if (!strcmp(key,"MESHCACHE")) {
    const bool hit=selectCachedMesh(strtoull(valueText,nullptr,16));
    if (hit) {
      meshReceiving=false; meshDisplayFailed=false; bootModelPending=false; loadingDesign=false;
      closeSettingsMenu(); restoreDisplayPending=true;
      loadingProgress.deleteSprite();
      if (!splashActive && !isClassicView(displayMode)) gemUi.begin(displayMode);
      ++telemetryVersion;
    }
    sendLineBoth(hit ? "@MESHACK,CACHE_HIT" : "@MESHACK,CACHE_MISS");
    return;
  }
  if (!strcmp(key, "GEMLOAD")) {
    char* title = strchr(valueText, ',');
    if (title) { *title++ = 0; snprintf(loadingTitle, sizeof(loadingTitle), "%.42s", title); }
    snprintf(loadingStage, sizeof(loadingStage), "%s", valueText);
    loadingDesign = strcmp(valueText, "ERROR") != 0;
    if (!loadingDesign) { meshDisplayFailed = true; bootModelPending = false; }
    if (!splashActive) drawLoadingProgress(-1, true);
    return;
  }
  if (!strncmp(key, "MESH", 4)) meshLastRxMs = millis();

  if (!strcmp(key, "MESHCLEAR"))
  {
    runtimeGemMesh().clear();
    meshReceiving = false;
    meshDisplayFailed = false;
    bootModelPending = false;
    loadingDesign = false;
    gemUi.invalidate();
    ++telemetryVersion;
    return;
  }

  if (!strcmp(key, "MESHBEGIN"))
  {
    char* save = nullptr;
    char* vertices = strtok_r(valueText, ",", &save);
    char* edges = strtok_r(nullptr, ",", &save);
    char* planes = strtok_r(nullptr, ",", &save);
    char* indexResolution = strtok_r(nullptr, ",", &save);
    char* radius = strtok_r(nullptr, ",", &save);
    char* indexSign = strtok_r(nullptr, ",", &save);
    bool ok = vertices && edges && planes && indexResolution && radius &&
              runtimeGemMesh().beginTransfer(
                  uint16_t(strtoul(vertices, nullptr, 10)),
                  uint16_t(strtoul(edges, nullptr, 10)),
                  uint16_t(strtoul(planes, nullptr, 10)),
                  strtof(indexResolution, nullptr), strtof(radius, nullptr));
    meshReceiving = ok;
    runtimeGemMesh().setIndexSign(indexSign ? atoi(indexSign) : 1);
    meshDisplayFailed = !ok;
    meshExpectedRows = uint32_t(runtimeGemMesh().vertices().size()) +
                      runtimeGemMesh().edges().size() + runtimeGemMesh().planes().size();
    meshCompletedRows = 0;
    meshNextVertex = meshNextEdge = meshNextPlane = 0;
    snprintf(loadingStage, sizeof(loadingStage), "Receiving model");
    if (!ok) sendLineBoth("@ERR,MESHBEGIN,invalid counts or scale");
    else {
      if (!splashActive) drawLoadingProgress(0);
      sendLineBoth("@MESHACK,BEGIN");
    }
    return;
  }

  if (!strcmp(key, "MESHV"))
  {
    char* save = nullptr;
    char* id = strtok_r(valueText, ",", &save);
    char* x = strtok_r(nullptr, ",", &save);
    char* y = strtok_r(nullptr, ",", &save);
    char* z = strtok_r(nullptr, ",", &save);
    if (meshReceiving && id && strtoul(id, nullptr, 10) < meshNextVertex) {
      acknowledgeMeshRow('V', uint16_t(strtoul(id, nullptr, 10))); return;
    }
    if (!id || !x || !y || !z ||
        !runtimeGemMesh().setVertex(uint16_t(strtoul(id, nullptr, 10)),
                                    strtof(x, nullptr), strtof(y, nullptr),
                                    strtof(z, nullptr)))
        { runtimeGemMesh().clear(); sendLineBoth("@ERR,MESHROW,bad vertex"); }
    else { ++meshNextVertex; ++meshCompletedRows; acknowledgeMeshRow('V', meshNextVertex - 1); }
    return;
  }

  if (!strcmp(key, "MESHE"))
  {
    char* save = nullptr;
    char* id = strtok_r(valueText, ",", &save);
    char* a = strtok_r(nullptr, ",", &save);
    char* b = strtok_r(nullptr, ",", &save);
    char* count = strtok_r(nullptr, ",", &save);
    if (meshReceiving && id && strtoul(id, nullptr, 10) < meshNextEdge) {
      acknowledgeMeshRow('E', uint16_t(strtoul(id, nullptr, 10))); return;
    }
    uint16_t supportPlanes[RUNTIME_MESH_MAX_EDGE_SUPPORTS] = {};
    uint8_t supportCount = count ? uint8_t(strtoul(count, nullptr, 10)) : 0;
    bool ok = id && a && b && count && supportCount >= 2 &&
              supportCount <= RUNTIME_MESH_MAX_EDGE_SUPPORTS;
    for (uint8_t support = 0; support < supportCount && ok; ++support)
    {
      char* plane = strtok_r(nullptr, ",", &save);
      if (!plane) { ok = false; break; }
      supportPlanes[support] = uint16_t(strtoul(plane, nullptr, 10));
    }
    if (!ok || !runtimeGemMesh().setEdge(
                   uint16_t(strtoul(id, nullptr, 10)),
                   uint16_t(strtoul(a, nullptr, 10)),
                   uint16_t(strtoul(b, nullptr, 10)), supportCount,
                   supportPlanes))
      { runtimeGemMesh().clear(); sendLineBoth("@ERR,MESHROW,bad edge"); }
    else { ++meshNextEdge; ++meshCompletedRows; acknowledgeMeshRow('E', meshNextEdge - 1); }
    return;
  }

  if (!strcmp(key, "MESHP"))
  {
    char* save = nullptr;
    char* id = strtok_r(valueText, ",", &save);
    char* tip = strtok_r(nullptr, ",", &save);
      char* twist = strtok_r(nullptr, ",", &save);
      char* tier = strtok_r(nullptr, ",", &save);
      char* facet = strtok_r(nullptr, ",", &save);
      char* name = strtok_r(nullptr, ",", &save);
      if (meshReceiving && id && strtoul(id, nullptr, 10) < meshNextPlane) {
        acknowledgeMeshRow('P', uint16_t(strtoul(id, nullptr, 10))); return;
      }
      if (!id || !tip || !twist || !tier || !facet ||
          !runtimeGemMesh().setPlane(
              uint16_t(strtoul(id, nullptr, 10)), strtof(tip, nullptr),
              strtof(twist, nullptr), uint16_t(strtoul(tier, nullptr, 10)),
              uint16_t(strtoul(facet, nullptr, 10)), name ? name : ""))
        { runtimeGemMesh().clear(); sendLineBoth("@ERR,MESHROW,bad plane"); }
      else { ++meshNextPlane; ++meshCompletedRows; acknowledgeMeshRow('P', meshNextPlane - 1); }
    return;
  }

  if (!strcmp(key, "MESHEND"))
  {
    if (!meshReceiving && runtimeGemMesh().active()) { sendLineBoth("@MESHACK,READY"); return; }
    meshReceiving = false;
    if (runtimeGemMesh().finishTransfer())
    {
      commitCachedMesh();
      meshDisplayFailed = false;
      bootModelPending = false;
      loadingDesign = false;
      closeSettingsMenu();
      restoreDisplayPending = true;
      loadingProgress.deleteSprite();
      if (!splashActive && !isClassicView(displayMode)) gemUi.begin(displayMode);
      ++telemetryVersion;
      sendLineBoth("@MESHACK,READY");
    }
    else {
      meshDisplayFailed = true;
      sendLineBoth("@ERR,MESHEND,incomplete transfer");
    }
    return;
  }

  if (!strcmp(key, "CFG") || !strcmp(key, "CFGACK"))
  {
    char* valueComma = strchr(valueText, ',');
    if (valueComma)
    {
      *valueComma = '\0';
      if (!strcasecmp(valueText, "SD_ACTIVE"))
      {
        snprintf(jobTitle, sizeof(jobTitle), "%s", valueComma + 1);
        jobActive = strcasecmp(jobTitle, "NONE") != 0;
        ++telemetryVersion;
      }
      else if (!strcasecmp(valueText, "LOAD_SD_FILE"))
      {
        snprintf(jobTitle, sizeof(jobTitle), "%s", valueComma + 1);
        jobActive = true;
        ++telemetryVersion;
      }
      settingsMenu.applyConfig(valueText, valueComma + 1);
    }
    return;
  }

  if (!strcmp(key, "CFGNAK"))
  {
    char* reasonComma = strchr(valueText, ',');
    if (reasonComma)
    {
      *reasonComma = '\0';
      settingsMenu.applyError(valueText, reasonComma + 1);
    }
    return;
  }

  if (!strcmp(key, "CFGSYNC"))
  {
    if(!strcmp(valueText,"BEGIN")) settingsMenu.setConfigSync(true);
    else if(!strcmp(valueText,"END")) settingsMenu.setConfigSync(false);
    return;
  }

  if (!strcmp(key, "ZRESET"))
  {
    encoder.reset(0);
    encCount = 0;
    lastEnc = 0;
    sendLineBoth("@ZENC,0");
    return;
  }

  if (!strcmp(key, "TIERCOMMENT")) {
    char* comma=strchr(valueText, ',');
    if(comma && strtoul(valueText,nullptr,10)==jobTier) {
      snprintf(tierComment,sizeof(tierComment),"%s",comma+1);
      ++telemetryVersion;
      gemUi.invalidate();
    }
    return;
  }
  if (!strcmp(key, "JOB"))
  {
    char* save = nullptr;
    char* tier = strtok_r(valueText, ",", &save);
    char* facet = strtok_r(nullptr, ",", &save);
    char* angle = strtok_r(nullptr, ",", &save);
    char* gemcadDistance = strtok_r(nullptr, ",", &save);
    char* index = strtok_r(nullptr, ",", &save);
    char* name = strtok_r(nullptr, ",", &save);
    if (tier && facet && angle && gemcadDistance && index)
    {
      const bool selectionChanged = jobTier != uint16_t(strtoul(tier, nullptr, 10)) ||
                                    jobFacet != uint16_t(strtoul(facet, nullptr, 10));
      jobTier = uint16_t(strtoul(tier, nullptr, 10));
      tierComment[0]=0;
      jobFacet = uint16_t(strtoul(facet, nullptr, 10));
      jobAngle = strtof(angle, nullptr);
      jobGemcadDistance = strtof(gemcadDistance, nullptr);
      jobIndex = strtof(index, nullptr);
      snprintf(jobFacetName, sizeof(jobFacetName), "%s", name ? name : "");
      jobActive = true;
      updateMarkPointsBool = true;
      ++telemetryVersion;
      if (selectionChanged) {
        char ack[64];
        snprintf(ack, sizeof(ack), "@JOBACK,%u,%u", unsigned(jobTier), unsigned(jobFacet));
        sendLineBoth(ack);
      }
    }
    return;
  }

  if (!strcmp(key, "MENU"))
  {
    if (!strcmp(valueText, "?"))
    {
      sendLineBoth(settingsMenu.isOpen() ? "@MENU,OPEN" : "@MENU,CLOSED");
      return;
    }

    if (!strcasecmp(valueText, "TOGGLE"))
    {
      if (settingsMenu.isOpen()) closeSettingsMenu();
      else openSettingsMenu();
    }
    else if (!strcmp(valueText, "1") || !strcasecmp(valueText, "OPEN"))
      openSettingsMenu();
    else if (!strcmp(valueText, "0") || !strcasecmp(valueText, "CLOSE"))
      closeSettingsMenu();
    else
      sendLineBoth("@ERR,MENU,expected TOGGLE|0|1|OPEN|CLOSE");
    return;
  }

  if (!strcmp(key, "MENUKEY"))
  {
    MenuKey menuKey;
    if (parseMenuKey(valueText, &menuKey))
      handleSettingsKey(menuKey);
    else
      sendLineBoth("@ERR,MENUKEY,expected UP|DOWN|SELECT|BACK|FINER|COARSER|DELETE");
    return;
  }

  if (!strcmp(key, "MODE"))
  {
    if (!strcmp(valueText, "?"))
    {
      reportDisplayMode("@MODE,");
      return;
    }

    DisplayMode nextMode;
    if (parseDisplayMode(valueText, &nextMode))
      saveDisplayModeAndRestart(nextMode);
    else
    {
      Serial.println("@ERR,MODE,expected CLASSIC|DYNAMIC|STATIC");
      baseSerial.println("@ERR,MODE,expected CLASSIC|DYNAMIC|STATIC");
    }
    return;
  }

  if (!strcmp(key, "PING"))
  {
    // Minimal request/reply used by the base before it starts telemetry. Echo
    // the token so unsolicited display traffic cannot fake a round trip.
    baseSerial.print("@PONG,");
    baseSerial.println(valueText);
    // Return the Z encoder in the same request/response cycle.  Rendering can
    // make loop-driven periodic reports irregular, but every base probe now
    // carries a fresh encoder sample back with it.
    encCount = encoder.getCount();
    lastEnc = encCount;
    lastEncoderTxMs = millis();
    baseSerial.print("@ZENC,");
    baseSerial.println(encCount);
    baseSerial.print("@ZIO,");
    baseSerial.println((digitalRead(10) ? 1 : 0) |
                       (digitalRead(11) ? 2 : 0));
    if (Serial)
    {
      Serial.print("@PONG,");
      Serial.println(valueText);
    }
    return;
  }

  if(!strcmp(key,"CHEAT")) {
    float nominal=0,cheat=0,target=0; int temporary=0;
    if(sscanf(valueText,"%f,%f,%d,%f",&nominal,&cheat,&temporary,&target)==4 &&
       isfinite(nominal) && isfinite(cheat) && isfinite(target)) {
      nominalIndexValue=nominal; cheatIndexValue=cheat;
      cheatTemporary=temporary!=0; cheatReceived=true;
      setTilt(target); ++telemetryVersion;
    }
    return;
  }
  float val = 0.0f;
  if (!parsePlainFloat(valueText, &val)) return;

  lastRx = millis();
  linkAlive = true;

  bool handled = true;

  if      (!strcmp(key, "T"))    setTilt(val);
  else if (!strcmp(key, "E"))    setTiltError(val);
  else if (!strcmp(key, "A")) {
    actualIndexValue = val;
    actualIndexReceived = true;
    ++telemetryVersion;
  }
  else if (!strcmp(key, "TIP") || !strcmp(key,"P")) {
    setTip(val); tipAngle=val; ++tipSampleSequence; // retain sub-display precision for history
  }
  else if (!strcmp(key,"HUDFAULT")) hudFaults=uint8_t(val);
  else if (!strcmp(key, "ZMM"))  setZ(val);
  else if (!strcmp(key, "Z"))    setZ(val);
  else if (!strcmp(key, "RPM"))  setRPMSet(val);
  else if (!strcmp(key, "RPV"))  setRPMActual(val);
  else if (!strcmp(key, "R"))    setRPMActual(val);
  else if (!strcmp(key, "DIR"))  setRPMDir(val);
  else if (!strcmp(key, "FLW"))  setFlow(val);
  else if (!strcmp(key, "FLD"))  setFlowDir(val);
  else if (!strcmp(key, "F"))    setForceRaw(val);
  else if (!strcmp(key, "N"))    setForceBar(val);
  else if (!strcmp(key, "TLK"))  setTiltLock(val);
  else if (!strcmp(key, "ZLK"))  setZLock(val);
  else if (!strcmp(key, "WIDX")) setWheelIndex(val);
  else if (!strcmp(key, "TIDX")) setTiltStepIndex(val);
  else if (!strcmp(key, "ZIDX")) setZedStepIndex(val);
  else if (!strcmp(key, "SSV"))  setSpinServo(val);
  else if (!strcmp(key, "MST"))  setMarkState(val);
  else if (!strcmp(key, "MID"))  setMarkIdx(val + 1.0f);
  else if (!strcmp(key, "MCT"))  setMarkCount(val);
  else if (!strcmp(key, "ML"))   setMarkLeft(val);
  else if (!strcmp(key, "MR"))   setMarkRight(val);
  else handled = false;

  if (handled) telemetryVersion++;
}

static void rxUpdateStream(Stream& port, char* buffer, uint8_t& index)
{
  uint16_t bytesRead = 0;
  uint8_t linesRead = 0;

  while (port.available() &&
         bytesRead < RX_BYTE_BUDGET &&
         linesRead < RX_LINE_BUDGET)
  {
    char c = port.read();
    bytesRead++;

    if (c == '\r') continue;

    if (c == '\n')
    {
      buffer[index] = '\0';

      if (index > 0)
        parseLine(buffer);

      index = 0;
      linesRead++;
    }
    else if (index < 127)
    {
      buffer[index++] = c;
    }
    else
    {
      index = 0;
    }
  }
}

void rxUpdate()
{
  if (!systemReady) return;

  rxUpdateStream(baseSerial, instrumentRxBuffer, instrumentRxIndex);
  rxUpdateStream(Serial, usbRxBuffer, usbRxIndex);

  if (millis() - lastRx > 1000)
    linkAlive = false;
}

// ================= ENCODER TX =================

void sendEncoder()
{
  if (!systemReady) return;

  encCount = encoder.getCount();

  // The legacy display returned the Z encoder on every telemetry poll.  Keep
  // change-triggered updates responsive, but also publish it periodically so
  // the base always acquires the initial value and can diagnose a stationary
  // encoder link.
  const uint32_t now = millis();
  if (encCount != lastEnc || now - lastEncoderTxMs >= 250)
  {
    lastEnc = encCount;
    lastEncoderTxMs = now;
    baseSerial.print("@ZENC,");
    baseSerial.println(encCount);
    Serial.print("@ZENC,");
    Serial.println(encCount);
  }
}

// ================= DRAWING =================

void drawSplashScreen()
{
  tft.fillScreen(TFT_BLACK);

  TFT_eSprite splash = TFT_eSprite(&tft);
  splash.setColorDepth(8);
  // Animation ends above the progress region (y=382); neither progress nor
  // the company footer is ever erased by a subsequent animation frame.
  if (!splash.createSprite(320, 370))
  {
    // A low-memory boot still gets a recognizable title instead of hanging.
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(0x3E7F, TFT_BLACK);
    tft.drawString("FACET HOUND", 160, 165);
    tft.setTextFont(1);
    tft.setTextColor(0x8410, TFT_BLACK);
    tft.drawString("A product of Advanced Precision Technologies, LLC", 160, 466);
    delay(3000);
    return;
  }
  splash.setTextWrap(false, false);
  splash.loadFont(ERRORTEXT);
  splash.setTextDatum(TL_DATUM);
  const int16_t titleWidth = splash.textWidth("FACET HOUND");
  const int16_t titleX = (320 - titleWidth) / 2;

  TFT_eSprite footer = TFT_eSprite(&tft);
  footer.setColorDepth(8);
  const bool footerReady = footer.createSprite(320, 20) != nullptr;
  if (footerReady)
  {
    footer.fillSprite(TFT_BLACK);
    footer.setTextWrap(false, false);
    footer.setTextDatum(MC_DATUM);
    footer.setTextFont(1);
    footer.setTextColor(0x8410, TFT_BLACK);
    footer.drawString("A product of Advanced Precision Technologies, LLC", 160, 10);
  }

  uint16_t selectedPlane = 0;
  for (uint16_t i = 0; i < GemData::kPlaneCount; ++i)
  {
    if (GemData::kPlanes[i].tier == 5 && GemData::kPlanes[i].facet == 1)
    {
      selectedPlane = i; // First crown tier: C1, facet 1.
      break;
    }
  }

  int16_t sx[GemData::kVertexCount] = {};
  int16_t sy[GemData::kVertexCount] = {};
  float depth[GemData::kVertexCount] = {};

  const uint32_t started = millis();
  uint32_t nextFrame = started;
  constexpr uint32_t kGemOnlyMs = 900;
  constexpr uint32_t kTotalMs = 3300;
  constexpr uint32_t kFrameMs = 33;
  bool footerShown = false;
  uint32_t finishAt = started + kTotalMs;

  while (int32_t(millis() - finishAt) < 0)
  {
    rxUpdate();
    if ((bootModelPending || loadingDesign || meshReceiving) && millis()-started < 20000)
      finishAt = millis() + 350;
    const uint32_t elapsed = millis() - started;
    const float brightness = constrain(float(int32_t(finishAt-millis())) / 300.0f, 0.0f, 1.0f);
    if (int32_t(millis() - nextFrame) < 0)
    {
      delay(1);
      yield();
      continue;
    }
    nextFrame += kFrameMs;

    splash.fillSprite(TFT_BLACK);

    // Hold the C1 mast angle while index rotates at a constant half-turn/sec.
    const float tip = -GemData::kPlanes[selectedPlane].tipDegrees * DEG_TO_RAD;
    const float spinTicks = GemData::kPlanes[selectedPlane].twistTicks +
                            float(elapsed) * float(GemData::kIndexResolution) / 2000.0f;
    const float twist = spinTicks * TWO_PI / float(GemData::kIndexResolution);
    const float ct = cosf(tip), st = sinf(tip);
    const float cz = cosf(twist), sz = sinf(twist);
    const float scale = 124.0f / GemData::kRadius;
    constexpr float centerX = 160.0f;
    constexpr float centerY = 202.0f;

    for (uint16_t i = 0; i < GemData::kVertexCount; ++i)
    {
      const float x1 = cz * GemData::kVertices[i].x - sz * GemData::kVertices[i].y;
      const float y1 = sz * GemData::kVertices[i].x + cz * GemData::kVertices[i].y;
      const float y2 = ct * y1 - st * GemData::kVertices[i].z;
      const float z2 = st * y1 + ct * GemData::kVertices[i].z;
      sx[i] = int16_t(lroundf(centerX + scale * x1));
      sy[i] = int16_t(lroundf(centerY - scale * y2));
      depth[i] = z2;
    }

    // Neutral depth-cued wireframe. C1 sets only the viewing angle.
    for (uint16_t i = 0; i < GemData::kEdgeCount; ++i)
    {
      const auto& edge = GemData::kEdges[i];
      const float normalized = constrain(
          0.5f + 0.5f * (depth[edge.a] + depth[edge.b]) /
                     (2.0f * GemData::kRadius), 0.0f, 1.0f);
      const uint8_t level = uint8_t((34.0f + normalized * 205.0f) * brightness);
      const uint16_t gray = uint16_t((level >> 3) << 11) |
                            uint16_t((level >> 2) << 5) |
                            uint16_t(level >> 3);
      splash.drawLine(sx[edge.a], sy[edge.a], sx[edge.b], sy[edge.b], gray);
    }

    if (elapsed >= kGemOnlyMs)
    {
      const float fade = constrain(float(elapsed - kGemOnlyMs) / 320.0f, 0.0f, 1.0f);
      const uint16_t titleColor = tft.color565(
          uint8_t((30.0f + 30.0f * fade) * brightness),
          uint8_t((80.0f + 150.0f * fade) * brightness),
          uint8_t((105.0f + 150.0f * fade) * brightness));
      const uint16_t accentColor = tft.color565(
          uint8_t((90.0f + 165.0f * fade) * brightness),
          uint8_t((20.0f + 45.0f * fade) * brightness),
          uint8_t((75.0f + 120.0f * fade) * brightness));

      // A black keyline and magenta offset keep the cyan title legible over
      // the wireframe without turning it into an opaque card.
      splash.setTextColor(TFT_BLACK);
      splash.drawString("FACET HOUND", titleX + 2, 112);
      splash.drawString("FACET HOUND", titleX - 2, 112);
      splash.setTextColor(accentColor);
      splash.drawString("FACET HOUND", titleX + 1, 111);
      splash.setTextColor(titleColor);
      splash.drawString("FACET HOUND", titleX, 108);
      splash.drawFastHLine(titleX, 138, titleWidth, accentColor);
      splash.fillCircle(titleX - 6, 138, 2, titleColor);
      splash.fillCircle(titleX + titleWidth + 6, 138, 2, titleColor);
    }

    splash.pushSprite(0, 0);
    if (bootModelPending || loadingDesign || meshReceiving)
      drawLoadingProgress(meshReceiving && meshExpectedRows ?
          int(100UL * meshCompletedRows / meshExpectedRows) : -1);
    if (elapsed >= kGemOnlyMs && footerReady && (!footerShown || brightness < 1.0f))
    {
      if (brightness < 1.0f) {
        footer.fillSprite(TFT_BLACK);
        const uint8_t gray = uint8_t(128.0f * brightness);
        footer.setTextColor(tft.color565(gray, gray, gray), TFT_BLACK);
        footer.drawString("A product of Advanced Precision Technologies, LLC", 160, 10);
      }
      footer.pushSprite(0, 456);
      footerShown = true;
    }
  }

  if (footerReady) footer.deleteSprite();
  splash.unloadFont();
  splash.deleteSprite();
  tft.fillScreen(TFT_BLACK);
}

void updateTiltSprite();
void updateTipSprite();
void updateZSprite();
void updateRPMSprite();
void updateFlowSprite();
void updateForceBarSprite();
void updateTiltErrorSprite();
void updateTiltLockSprite();
void updateWheelIndexSprite();
void updateZedLockSprite();
void updateRPMDirSprite();
void updateFlowDirSprite();
void updateZedStepIndexSprite();
void updateTiltStepIndexSprite();
void updateStepServoSprite();
void updateRPMSetValueSprite();
void updateMarkPointsSprite();
void updateMarkStatusSprite();
void updateAllSprites();

void drawMainScreen(bool initHere)
{
  if (initHere)
  {
    tft.fillScreen(TFT_BLACK);

    unitLabels.createSprite(30, 370);
    unitLabels.loadFont(LABELS);
    unitLabels.setTextDatum(BL_DATUM);
    unitLabels.setTextColor(0xEF00);
    unitLabels.drawString("°", 0, displayMode==DisplayMode::CLASSIC_TIER ? 205 : 230);
    unitLabels.setTextColor(0xF8F4);
    unitLabels.drawString("mm", 0, 368);
    unitLabels.pushSprite(290, 0);

    axisLabels.createSprite(25, 370);
    axisLabels.setTextDatum(BR_DATUM);
    axisLabels.loadFont(LABELS);
    axisLabels.setTextColor(0x07FE);
    axisLabels.drawString("Φ", 22, 50);
    axisLabels.setTextColor(0xEF00);
    axisLabels.drawString("Θ", 22, displayMode==DisplayMode::CLASSIC_TIER ? 245 : 270);
    axisLabels.setTextColor(0xF8F4);
    axisLabels.drawString("z", 22, 368);
    axisLabels.pushSprite(0, 0);

    downLabels.createSprite(320, 25);
    downLabels.setTextDatum(BR_DATUM);
    downLabels.loadFont(LABELS);
    downLabels.setTextColor(0xFFFF);
    downLabels.drawString("RPM", 100, 27);
    downLabels.drawString("mL/min", 300, 27);
    downLabels.pushSprite(0, 450);

    stext1.setColorDepth(8);
    stext1.loadFont(NUMBERS);
    stext1.createSprite(205, 60);
    stext1.setTextColor(0x07FE);
    stext1.setTextDatum(BR_DATUM);

    stext2.setColorDepth(8);
    stext2.createSprite(displayMode==DisplayMode::CLASSIC_TIER ? 175 : 256, 60);
    stext2.loadFont(NUMBERS);
    stext2.setTextColor(0xEF00);
    stext2.setTextDatum(BR_DATUM);

    stext3.setColorDepth(8);
    stext3.createSprite(256, displayMode==DisplayMode::CLASSIC_TIER ? 95 : 110);
    stext3.loadFont(NUMBERS);
    stext3.setTextColor(0xF8F4);
    stext3.setTextDatum(BR_DATUM);

    stext4.setColorDepth(8);
    stext4.createSprite(140, 70);
    stext4.loadFont(NUMBERS_SMALL);
    stext4.setTextColor(0xFE19);
    stext4.setTextDatum(BR_DATUM);

    stext5.setColorDepth(8);
    stext5.createSprite(140, 70);
    stext5.loadFont(NUMBERS_SMALL);
    stext5.setTextColor(0xC618);
    stext5.setTextDatum(BR_DATUM);

    sBar1.setColorDepth(8);
    sBar1.createSprite(300, displayMode==DisplayMode::CLASSIC_TIER ? 40 : 20);
    if(displayMode==DisplayMode::CLASSIC_TIER) {
      sBar1.loadFont(LABELS); // Same smooth font and size as the index list.
      tierError.setColorDepth(8);
      tierError.createSprite(110,40);
      tierError.loadFont(ERRORTEXT);
      tierError.setTextDatum(BR_DATUM);
      tierError.setTextColor(0xEF00);
    }

    stext6.setColorDepth(8);
    stext6.createSprite(110, 40);
    stext6.loadFont(ERRORTEXT);
    stext6.setTextColor(0x07FE);
    stext6.setTextDatum(BR_DATUM);

    stext7.setColorDepth(8);
    stext7.createSprite(30, 30);
    stext7.loadFont(LABELS);
    stext7.setTextColor(0x07FE);
    stext7.setTextDatum(BR_DATUM);

    stext8.setColorDepth(8);
    stext8.createSprite(30, 30);
    stext8.loadFont(LABELS);
    stext8.setTextColor(0xF8F4);
    stext8.setTextDatum(BR_DATUM);

    stext9.setColorDepth(8);
    stext9.createSprite(50, 30);
    stext9.loadFont(LABELS);
    stext9.setTextColor(0x07FE);
    stext9.setTextDatum(BR_DATUM);

    stext10.setColorDepth(8);
    stext10.createSprite(30, 30);
    stext10.loadFont(LABELS);
    stext10.setTextColor(0xC618);
    stext10.setTextDatum(BR_DATUM);

    stext11.setColorDepth(8);
    stext11.createSprite(30, 30);
    stext11.loadFont(LABELS);
    stext11.setTextColor(0xC618);
    stext11.setTextDatum(BR_DATUM);

    stext12.setColorDepth(8);
    stext12.createSprite(30, 30);
    stext12.loadFont(LABELS);
    stext12.setTextColor(0xF8F4);
    stext12.setTextDatum(BR_DATUM);

    stext13.setColorDepth(8);
    stext13.createSprite(30, 30);
    stext13.loadFont(LABELS);
    stext13.setTextColor(0x07FE);
    stext13.setTextDatum(BR_DATUM);

    stext14.setColorDepth(8);
    stext14.createSprite(30, 30);
    stext14.loadFont(LABELS);
    stext14.setTextColor(0x07FE);
    stext14.setTextDatum(BR_DATUM);

    stext15.setColorDepth(8);
    stext15.createSprite(60, 30);
    stext15.loadFont(LABELS);
    stext15.setTextColor(0xC618);
    stext15.setTextDatum(BR_DATUM);

    markSprite.setColorDepth(8);
    markSprite.createSprite(300, 30);
    markSprite.loadFont(LABELS);
    markSprite.setTextColor(0x07FE);
    markSprite.setTextDatum(BR_DATUM);

    markStatusSprite.setColorDepth(8);
    markStatusSprite.createSprite(30, 30);
    markStatusSprite.loadFont(LABELS);
    markStatusSprite.setTextColor(0x07FE);
    markStatusSprite.setTextDatum(BR_DATUM);
  }

  updateAllSprites();
}

void spriteTickHandler()
{
  if(displayMode==DisplayMode::CLASSIC_TIER) {
    updateForceBarSprite();
    static uint32_t lastTierMarks=UINT32_MAX;
    if(lastTierMarks!=telemetryVersion) {
      lastTierMarks=telemetryVersion;
      updateMarkPointsSprite();
    }
  }
  if (!updateTipAngle &&
      !updateTiltAngle &&
      !updateTiltError &&
      !updateZValue &&
      !updateRPMValue &&
      !updateFlowRate &&
      !updateForceBarBool &&
      !updateTiltLockBool &&
      !updateZedLockBool &&
      !updateWheelIndexBool &&
      !updateFlowDirBool &&
      !updateRPMDirBool &&
      !updateTiltStepIndexBool &&
      !updateSpinServoIdxBool &&
      !updateZedStepIndexBool &&
      !updateRPMsetValueBool &&
      !updateMarkPointsBool &&
      !updateMarkStatusBool)
  {
    return;
  }

  tft.startWrite();

  if (updateTipAngle) { updateTipSprite(); updateTipAngle = false; }
  if (updateTiltAngle) { updateTiltSprite(); updateTiltAngle = false; }
  if (updateTiltError) { updateTiltErrorSprite(); updateTiltError = false; }
  if (updateZValue) { updateZSprite(); updateZValue = false; }
  if (updateRPMValue) { updateRPMSprite(); updateRPMValue = false; }
  if (updateFlowRate) { updateFlowSprite(); updateFlowRate = false; }
  if (updateForceBarBool) { updateForceBarSprite(); updateForceBarBool = false; }
  if (updateTiltLockBool) { updateTiltLockSprite(); updateTiltLockBool = false; }
  if (updateZedLockBool) { updateZedLockSprite(); updateZedLockBool = false; }
  if (updateWheelIndexBool) { updateWheelIndexSprite(); updateWheelIndexBool = false; }
  if (updateFlowDirBool) { updateFlowDirSprite(); updateFlowDirBool = false; }
  if (updateRPMDirBool) { updateRPMDirSprite(); updateRPMDirBool = false; }
  if (updateTiltStepIndexBool) { updateTiltStepIndexSprite(); updateTiltStepIndexBool = false; }
  if (updateSpinServoIdxBool) { updateStepServoSprite(); updateSpinServoIdxBool = false; }
  if (updateZedStepIndexBool) { updateZedStepIndexSprite(); updateZedStepIndexBool = false; }
  if (updateRPMsetValueBool) { updateRPMSetValueSprite(); updateRPMsetValueBool = false; }
  if (updateMarkPointsBool) { updateMarkPointsSprite(); updateMarkPointsBool = false; }
  if (updateMarkStatusBool) { updateMarkStatusSprite(); updateMarkStatusBool = false; }

  tft.endWrite();
}

void updateAllSprites()
{
  tft.startWrite();

  updateTiltSprite();
  updateTipSprite();
  updateZSprite();
  updateRPMSprite();
  updateFlowSprite();
  updateForceBarSprite();
  updateTiltErrorSprite();
  updateTiltLockSprite();
  updateWheelIndexSprite();
  updateZedLockSprite();
  updateZedStepIndexSprite();
  updateRPMDirSprite();
  updateFlowDirSprite();
  updateStepServoSprite();
  updateTiltStepIndexSprite();
  updateRPMSetValueSprite();
  updateMarkPointsSprite();
  updateMarkStatusSprite();

  tft.endWrite();

  updateTiltAngle = false;
  updateTiltError = false;
  updateTiltLockBool = false;
  updateTiltStepIndexBool = false;
  updateMarkPointsBool = false;
  updateMarkStatusBool = false;
  updateSpinServoIdxBool = false;
  updateTipAngle = false;
  updateZValue = false;
  updateZedLockBool = false;
  updateZedStepIndexBool = false;
  updateRPMValue = false;
  updateRPMsetValueBool = false;
  updateRPMDirBool = false;
  updateFlowRate = false;
  updateFlowDirBool = false;
  updateForceBarBool = false;
  updateWheelIndexBool = false;
}

void updateTiltSprite()
{
  stext1.fillSprite(SPRITE_FILL);
  stext1.drawFloat(tiltSetAngle, 2, 205, 75);
  stext1.pushSprite(5, 60);
}

void updateTipSprite()
{
  stext2.fillSprite(SPRITE_FILL);
  stext2.drawFloat(tipAngle, 2, displayMode==DisplayMode::CLASSIC_TIER ? 175 : 256, 75);
  stext2.pushSprite(30, displayMode==DisplayMode::CLASSIC_TIER ? 185 : 210);
}

void updateZSprite()
{
  stext3.fillSprite(SPRITE_FILL);
  stext3.drawFloat(zValue, 3, 256, displayMode==DisplayMode::CLASSIC_TIER ? 95 : 110);
  stext3.pushSprite(30, displayMode==DisplayMode::CLASSIC_TIER ? 285 : 270);
}

void updateRPMSprite()
{
  stext4.fillSprite(SPRITE_FILL);
  // The large field is measured RPM.  The separate small field below is the
  // requested RPM and remains visible in both moving and paused states.
  stext4.drawNumber(RPMValue, 120, 60);
  stext4.pushSprite(5, 380);
}

void updateFlowSprite()
{
  stext5.fillSprite(SPRITE_FILL);
  stext5.drawFloat(flowRate, 1, 140, 60);
  stext5.pushSprite(170, 380);
}

void updateForceBarSprite()
{
  if(displayMode==DisplayMode::CLASSIC_TIER) {
    static uint32_t lastCheck=0;
    if(millis()-lastCheck<100) return;
    lastCheck=millis();
    const auto& mesh=runtimeGemMesh();
    uint16_t tiers[512]={}, planes[512]={}; unsigned count=0;
    const unsigned planeCount=mesh.active()?mesh.planes().size():GemData::kPlaneCount;
    for(unsigned p=0;p<planeCount;++p) {
      uint16_t tier=mesh.active()?mesh.planes()[p].tier:GemData::kPlanes[p].tier;
      bool seen=false; for(unsigned i=0;i<count;++i) if(tiers[i]==tier) seen=true;
      if(!seen && count<512) {
        unsigned pos=count;
        while(pos && tiers[pos-1]>tier) {tiers[pos]=tiers[pos-1];planes[pos]=planes[pos-1];--pos;}
        tiers[pos]=tier; planes[pos]=p; ++count;
      }
    }
    if(!count) return;
    unsigned current=0; for(unsigned i=0;i<count;++i) if(tiers[i]==jobTier) current=i;
    unsigned left=(current+count-1)%count,right=(current+1)%count;
    char prev[16],next[16],selected[16],text[80],error[80];
    auto nameAt=[&](unsigned i,char* out,size_t size) {
      if(mesh.active() && mesh.planes()[planes[i]].name[0])
        snprintf(out,size,"%s",mesh.planes()[planes[i]].name);
      else gemUi.inferredTierName(planes[i],out,size);
    };
    nameAt(left,prev,sizeof(prev)); nameAt(right,next,sizeof(next));
    nameAt(current,selected,sizeof(selected));
    float target=fabsf(jobAngle); if(target>90) target=180-target;
    snprintf(text,sizeof(text),"%s < %s %u/%u > %s",prev,selected,current+1,count,next);
    snprintf(error,sizeof(error),"T%u  %.2f   err %+.2f",tiers[current],target,tipAngle-target);
    static char previous[160]={}; char combined[160]; snprintf(combined,sizeof(combined),"%s%s",text,error);
    static uint32_t lastTierDraw=0;
    if(!strcmp(previous,combined) && millis()-lastTierDraw<500) return;
    snprintf(previous,sizeof(previous),"%s",combined); lastTierDraw=millis();
    sBar1.fillSprite(SPRITE_FILL); sBar1.setTextDatum(BC_DATUM);
    sBar1.setTextColor(TFT_YELLOW,SPRITE_FILL);
    // LABELS has no angle-bracket glyphs: draw the same chevrons as index.
    char middle[40],row[96];
    snprintf(middle,sizeof(middle),"%s %u/%u",selected,current+1,count);
    snprintf(row,sizeof(row),"%s   %s   %s",prev,middle,next);
    while(sBar1.textWidth(row)>296 && (strlen(prev)>1 || strlen(next)>1)) {
      char* longest=strlen(prev)>=strlen(next)?prev:next;
      longest[strlen(longest)-1]='\0';
      snprintf(row,sizeof(row),"%s   %s   %s",prev,middle,next);
    }
    const int gap=sBar1.textWidth("   ");
    const int start=(300-sBar1.textWidth(row))/2;
    const int leftArrow=start+sBar1.textWidth(prev)+gap/2;
    const int rightArrow=leftArrow+gap+sBar1.textWidth(middle);
    sBar1.drawString(row,150,30);
    for(int stroke=0;stroke<2;++stroke) {
      sBar1.drawLine(leftArrow+3+stroke,7,leftArrow-3+stroke,15,TFT_YELLOW);
      sBar1.drawLine(leftArrow-3+stroke,15,leftArrow+3+stroke,23,TFT_YELLOW);
      sBar1.drawLine(rightArrow-3+stroke,7,rightArrow+3+stroke,15,TFT_YELLOW);
      sBar1.drawLine(rightArrow+3+stroke,15,rightArrow-3+stroke,23,TFT_YELLOW);
    }
    char errorText[24];
    snprintf(errorText,sizeof(errorText),"%+.2f",tipAngle-target);
    tierError.fillSprite(SPRITE_FILL);
    tierError.drawFloat(fabsf(tipAngle-target),2,100,40);
    tierError.drawString(tipAngle>=target ? "+" : "-",15,40);
    tierError.drawCircle(106,10,2,0xEF00);
    tierError.pushSprite(210,205);
    sBar1.pushSprite(10,255);
    return;
  }
  int lastFillBarWidth = fillBarWidth;
  fillBarWidth = clampInt((forceBar * 280) / 20, 0, 280);

  if (lastFillBarWidth != fillBarWidth || forceBar == 0)
  {
    sBar1.fillSprite(SPRITE_FILL);
    sBar1.drawRect(0, 0, 280, 10, 0x00FF00);
    sBar1.fillRect(0, 0, fillBarWidth, 10, 0x00FF00);
    sBar1.pushSprite(20, 170);
  }
}

void updateTiltErrorSprite()
{
  stext6.fillSprite(SPRITE_FILL);
  stext6.drawFloat(fabsf(tiltSetError), 2, 110, 40);
  stext6.drawString((tiltSetError >= 0.0f) ? "+" : "-", 15, 40);
  stext6.pushSprite(210, 87);
}

void updateTiltLockSprite()
{
  stext7.fillSprite(SPRITE_FILL);
  stext7.drawString(tiltLock ? "X" : "-", 20, 30);
  stext7.pushSprite(40, 20);
}

void updateWheelIndexSprite()
{
  stext9.fillSprite(SPRITE_FILL);
  stext9.drawNumber((int)wheelIndex, 48, 30);
  stext9.pushSprite(175, 20);
}

void updateZedLockSprite()
{
  stext8.fillSprite(SPRITE_FILL);
  stext8.drawString(zedLock ? "X" : "-", 20, 30);
  stext8.pushSprite(2, 305);
}

void updateRPMDirSprite()
{
  stext10.fillSprite(SPRITE_FILL);

  switch (RPM_dir)
  {
    case 0: stext10.drawString("l", 20, 30); break; // left, paused
    case 1: stext10.drawString("R", 20, 30); break; // right, moving
    case 2: stext10.drawString("r", 20, 30); break; // right, paused
    case 3: stext10.drawString("L", 20, 30); break; // left, moving
  }

  stext10.pushSprite(10, 447);
}

void updateFlowDirSprite()
{
  stext11.fillSprite(SPRITE_FILL);

  switch (Flow_dir)
  {
    case 0: stext11.drawString("U", 20, 30); break;
    case 1: stext11.drawString("u", 20, 30); break;
    case 2: stext11.drawString("D", 20, 30); break;
    case 3: stext11.drawString("d", 20, 30); break;
  }

  stext11.pushSprite(190, 447);
}

void updateZedStepIndexSprite()
{
  stext12.fillSprite(SPRITE_FILL);
  drawStepIcon(stext12, zedStepIndex, 0xF8F4);
  stext12.pushSprite(290, 305);
}

void updateTiltStepIndexSprite()
{
  stext13.fillSprite(SPRITE_FILL);
  drawStepIcon(stext13, tiltStepIndex, 0x07FE);
  stext13.pushSprite(290, 20);
}

void updateStepServoSprite()
{
  stext14.fillSprite(SPRITE_FILL);

  if (spinServoIdx == 0) stext14.drawString("K", 20, 30);
  else if (spinServoIdx == 1) stext14.drawString("S", 20, 30);
  else stext14.drawString("-", 20, 30);

  stext14.pushSprite(80, 20);
}

void updateRPMSetValueSprite()
{
  stext15.fillSprite(SPRITE_FILL);
  stext15.drawNumber(rpmSetValue, 60, 30);
  stext15.pushSprite(110, 445);
}

void updateMarkPointsSprite()
{
    float leftValue=markL,rightValue=markR;
    int ordinal=markIdx,total=nMarkIdx;
    if(displayMode==DisplayMode::CLASSIC_TIER) {
      const auto& mesh=runtimeGemMesh();
      const unsigned count=mesh.active()?mesh.planes().size():GemData::kPlaneCount;
      float indices[512]; uint16_t facets[512]; unsigned n=0;
      for(unsigned p=0;p<count && n<512;++p) {
        const auto tier=mesh.active()?mesh.planes()[p].tier:GemData::kPlanes[p].tier;
        if(tier!=jobTier) continue;
        float index=mesh.active()?mesh.planes()[p].twistTicks:GemData::kPlanes[p].twistTicks;
        const float angle=mesh.active()?mesh.planes()[p].tipDegrees:GemData::kPlanes[p].tipDegrees;
        const float wheel=mesh.active()?mesh.indexResolution():96.0f;
        if(angle<0 || angle>90) index+=wheel*.5f;
        index=fmodf(index+wheel,wheel);
        if(wheelIndex>0 && wheel>0) index*=wheelIndex/wheel;
        unsigned pos=n;
        while(pos && indices[pos-1]>index) { indices[pos]=indices[pos-1]; facets[pos]=facets[pos-1]; --pos; }
        indices[pos]=index;
        facets[pos]=mesh.active()?mesh.planes()[p].facet:GemData::kPlanes[p].facet;
        ++n;
      }
      if(n) {
        unsigned selected=0;
        for(unsigned i=0;i<n;++i) if(facets[i]==jobFacet) selected=i;
        ordinal=selected+1; total=n;
        leftValue=indices[(selected+n-1)%n]; rightValue=indices[(selected+1)%n];
      }
    }
    markSprite.fillSprite(SPRITE_FILL);
    char counter[32]; snprintf(counter, sizeof(counter), "%d/%d", ordinal,total);
    markSprite.setTextDatum(BC_DATUM);
    if (markSprite.textWidth(counter) > 96) {
      markSprite.unloadFont();
      markSprite.setTextFont(2);
      markSprite.drawString(counter, 155, 30);
      markSprite.loadFont(LABELS);
    } else markSprite.drawString(counter, 155, 30);
    markSprite.setTextDatum(BR_DATUM);
  markSprite.drawFloat(leftValue, 2, 80, 30);
  markSprite.drawLine(105, 7, 99, 15, 0x07FE);
  markSprite.drawLine(99, 15, 105, 23, 0x07FE);
  markSprite.drawLine(106, 7, 100, 15, 0x07FE);
  markSprite.drawLine(100, 15, 106, 23, 0x07FE);
  markSprite.drawLine(210, 7, 216, 15, 0x07FE);
  markSprite.drawLine(216, 15, 210, 23, 0x07FE);
  markSprite.drawLine(211, 7, 217, 15, 0x07FE);
  markSprite.drawLine(217, 15, 211, 23, 0x07FE);
  markSprite.drawFloat(rightValue, 2, 300, 30);
  markSprite.pushSprite(10, 130);
}

void updateMarkStatusSprite()
{
  markStatusSprite.fillSprite(SPRITE_FILL);
  markStatusSprite.drawString(markStateLabel(), 20, 30);
  markStatusSprite.pushSprite(260, 58);
}

// ================= SETUP / LOOP =================

static GemTelemetry currentGemTelemetry()
{
  GemTelemetry state;
  state.targetTwist = tiltSetAngle;
  state.actualTwist = actualIndexValue;
  state.actualTwistValid = actualIndexReceived;
  state.twistError = tiltSetError;
  state.tipDegrees = tipAngle;
  state.tipSampleSequence=tipSampleSequence;
  state.nominalIndex=nominalIndexValue;
  state.cheatIndex=cheatIndexValue;
  state.cheatReceived=cheatReceived;
  state.cheatTemporary=cheatTemporary;
  state.faults=hudFaults;
  state.zMillimeters = zValue;
  state.flow = flowRate;
  state.wheelIndex = wheelIndex;
  state.rpmActual = RPMValue;
  state.rpmSet = rpmSetValue;
  state.forceBar = forceBar;
  state.rpmDirection = RPM_dir;
  state.flowDirection = Flow_dir;
  state.indexStep = tiltStepIndex;
  state.zStep = zedStepIndex;
  state.indexEngaged = tiltLock != 0;
  state.zEngaged = zedLock != 0;
  state.jobActive = jobActive;
  state.jobTitle = jobTitle;
  state.jobFacetName = jobFacetName;
  state.tierComment = tierComment;
  state.jobTier = jobTier;
  state.jobFacet = jobFacet;
  state.jobAngle = jobAngle;
  state.jobGemcadDistance = jobGemcadDistance;
  state.jobIndex = jobIndex;
  state.linkAlive = linkAlive;
  state.version = telemetryVersion;
  return state;
}

static void restoreDisplayAfterSettings()
{
  restoreDisplayPending = false;

  if (isClassicView(displayMode))
  {
    drawMainScreen(true);
    fillBarWidth = -1;
    updateAllSprites();
  }
  else
  {
    gemUi.invalidate();
  }
}

void setup()
{
  Serial.begin(115200);
  baseSerial.begin(DISPLAY_BAUD);
  Serial.println("@BUILD,DISPLAY,INTEGRATION-RELEASE-20260918");
  baseSerial.println("@BUILD,DISPLAY,INTEGRATION-RELEASE-20260918");

  // The animated title card is useful on the bench, but it should not hold up
  // an installed display.  Treat USB mode as an actively opened CDC port, not
  // merely USB power being present.  A short window lets the host finish CDC
  // enumeration after reset without adding much delay to an instrument boot.
  const uint32_t usbDetectStarted = millis();
  while (!Serial && millis() - usbDetectStarted < 750)
  {
    delay(5);
    yield();
  }
  usbDemoMode = bool(Serial);

  EEPROM.begin(256);
  displayMode = loadDisplayMode();

  tft.init();
  tft.setRotation(2);
  systemReady = true;
  if (displayMode == DisplayMode::CLASSIC) {
    bootModelPending=false; // Machine-status mode has no model dependency.
  } else {
    splashActive = true;
    sendLineBoth("@CFGGET,MESH");
    drawSplashScreen();
    splashActive = false;
  }

  if (bootModelPending || meshReceiving || loadingDesign || meshDisplayFailed) {
    drawLoadingProgress(-1,true);
    restoreDisplayPending = true;
  }
  else if (isClassicView(displayMode))
  {
    drawMainScreen(true);
  }
  else
  {
    loadingProgress.deleteSprite();
    gemUi.begin(displayMode);
    gemUi.tick(currentGemTelemetry());
  }

  encoder.begin();
  // PioEncoder enables internal pull-ups. They load the PCB's 100k/200k
  // dividers enough to prevent encoder lows reaching a valid logic low.
  // Disable pulls after begin(), which otherwise re-enables them.
  gpio_disable_pulls(10);
  gpio_disable_pulls(11);
  delay(50);

  systemReady = true;
  baseSerial.print("@FW,DISPLAY,");
  baseSerial.println(DISPLAY_FIRMWARE_ID);
  baseSerial.println("@HELLO,DISPLAY");
  reportDisplayMode("@MODE,");
  if (usbDemoMode)
  {
    Serial.print("@FW,DISPLAY,");
    Serial.println(DISPLAY_FIRMWARE_ID);
    Serial.println("@HELLO,DISPLAY,USB_DEMO");
  }
  if (bootModelPending && !meshReceiving) sendLineBoth("@CFGGET,MESH");
}

void loop()
{
  rxUpdate();
  sendEncoder();
  if (meshReceiving && millis() - meshLastRxMs > 3000) {
    meshReceiving = false;
    meshDisplayFailed = true;
    sendLineBoth("@ERR,MESHEND,transfer timeout");
  }
  // Do not spend tens of milliseconds drawing while the mesh is arriving.
  // Never show the built-in mesh with a loaded job's points after a failure.
  if (meshReceiving || meshDisplayFailed || loadingDesign || bootModelPending) {
    if (!meshDisplayFailed) drawLoadingProgress(meshReceiving && meshExpectedRows ?
        int(100UL * meshCompletedRows / meshExpectedRows) : -1);
    if (meshDisplayFailed) {
      static uint32_t lastErrorDraw = 0;
      if (millis() - lastErrorDraw > 1000) {
        tft.fillRect(0, 0, 320, 48, TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("Gem transfer failed - reload", 8, 8, 2);
        lastErrorDraw = millis();
      }
    }
    yield();
    return;
  }

  loadingProgress.deleteSprite(); // Reclaim temporary receive UI RAM.

  // This heartbeat gives the base a continuous, non-motion-dependent way to
  // verify the display-to-base half of the UART.  Encoder traffic alone is not
  // sufficient because a stationary encoder sends nothing.
  static uint32_t lastDisplayHeartbeat = 0;
  if (millis() - lastDisplayHeartbeat >= 1000)
  {
    baseSerial.println("@HELLO,DISPLAY");
    baseSerial.println(settingsMenu.isOpen() ? "@MENU,OPEN" : "@MENU,CLOSED");
    lastDisplayHeartbeat = millis();
  }

  static uint32_t lastIndexSpinKeepalive = 0;
  if (settingsMenu.isOpen() && settingsMenu.indexSpinRunning() &&
      millis() - lastIndexSpinKeepalive >= 500)
  {
    char line[48] = {};
    snprintf(line, sizeof(line), "@CFGACTION,INDEX_SPIN_START,%.2f",
             settingsMenu.indexSpinRpm());
    sendLineBoth(line);
    lastIndexSpinKeepalive = millis();
  }

  if (restoreDisplayPending)
    restoreDisplayAfterSettings();

  if (settingsMenu.isOpen())
  {
    yield();
    return;
  }

  if (isClassicView(displayMode))
    spriteTickHandler();
  else
    gemUi.tick(currentGemTelemetry());

  yield();
}
