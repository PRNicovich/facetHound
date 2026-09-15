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

#include "display_mode.h"
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

SerialPIO baseSerial(TXPin, RXPin);
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
float wheelIndex = 96.0f;
bool updateWheelIndexBool = true;

bool jobActive = false;
char jobTitle[48] = {};
char jobFacetName[12] = {};
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
DisplayMode displayMode = DisplayMode::CLASSIC;
bool restoreDisplayPending = false;

static const uint32_t DISPLAY_BAUD = 460800;
static const uint16_t RX_BYTE_BUDGET = 320;
static const uint8_t RX_LINE_BUDGET = 12;
static const uint32_t SETTINGS_MAGIC = 0x46484D36; // "FHM6"

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
      settings.mode > static_cast<uint8_t>(DisplayMode::STATIC))
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

  Serial.print("@MODE,RESTARTING,");
  Serial.println(displayModeName(nextMode));
  baseSerial.print("@MODE,RESTARTING,");
  baseSerial.println(displayModeName(nextMode));
  delay(80);
  rp2040.reboot();
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
}

static void setRPMDir(float v)
{
  int next = clampInt((int)lroundf(v), 0, 3);
  if (!displayedIntChanged(RPM_dir, next)) return;
  RPM_dir = next;
  updateRPMDirBool = true;
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

  if (!strcmp(key, "MESHCLEAR"))
  {
    runtimeGemMesh().clear();
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
    bool ok = vertices && edges && planes && indexResolution && radius &&
              runtimeGemMesh().beginTransfer(
                  uint16_t(strtoul(vertices, nullptr, 10)),
                  uint16_t(strtoul(edges, nullptr, 10)),
                  uint16_t(strtoul(planes, nullptr, 10)),
                  strtof(indexResolution, nullptr), strtof(radius, nullptr));
    if (!ok) sendLineBoth("@ERR,MESHBEGIN,invalid counts or scale");
    return;
  }

  if (!strcmp(key, "MESHV"))
  {
    char* save = nullptr;
    char* id = strtok_r(valueText, ",", &save);
    char* x = strtok_r(nullptr, ",", &save);
    char* y = strtok_r(nullptr, ",", &save);
    char* z = strtok_r(nullptr, ",", &save);
    if (!id || !x || !y || !z ||
        !runtimeGemMesh().setVertex(uint16_t(strtoul(id, nullptr, 10)),
                                    strtof(x, nullptr), strtof(y, nullptr),
                                    strtof(z, nullptr)))
        runtimeGemMesh().clear();
    return;
  }

  if (!strcmp(key, "MESHE"))
  {
    char* save = nullptr;
    char* id = strtok_r(valueText, ",", &save);
    char* a = strtok_r(nullptr, ",", &save);
    char* b = strtok_r(nullptr, ",", &save);
    char* count = strtok_r(nullptr, ",", &save);
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
      runtimeGemMesh().clear();
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
      if (!id || !tip || !twist || !tier || !facet ||
          !runtimeGemMesh().setPlane(
              uint16_t(strtoul(id, nullptr, 10)), strtof(tip, nullptr),
              strtof(twist, nullptr), uint16_t(strtoul(tier, nullptr, 10)),
              uint16_t(strtoul(facet, nullptr, 10)), name ? name : ""))
        runtimeGemMesh().clear();
    return;
  }

  if (!strcmp(key, "MESHEND"))
  {
    if (runtimeGemMesh().finishTransfer())
    {
      gemUi.invalidate();
      ++telemetryVersion;
      sendLineBoth("@MESHACK,READY");
    }
    else
      sendLineBoth("@ERR,MESHEND,incomplete transfer");
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
    return;

  if (!strcmp(key, "ZRESET"))
  {
    encoder.reset(0);
    encCount = 0;
    lastEnc = 0;
    sendLineBoth("@ZENC,0");
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
      jobTier = uint16_t(strtoul(tier, nullptr, 10));
      jobFacet = uint16_t(strtoul(facet, nullptr, 10));
      jobAngle = strtof(angle, nullptr);
      jobGemcadDistance = strtof(gemcadDistance, nullptr);
      jobIndex = strtof(index, nullptr);
      snprintf(jobFacetName, sizeof(jobFacetName), "%s", name ? name : "");
      jobActive = true;
      updateMarkPointsBool = true;
      ++telemetryVersion;
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

    if (!strcmp(valueText, "1") || !strcasecmp(valueText, "OPEN"))
      openSettingsMenu();
    else if (!strcmp(valueText, "0") || !strcasecmp(valueText, "CLOSE"))
      closeSettingsMenu();
    else
      sendLineBoth("@ERR,MENU,expected 0|1|OPEN|CLOSE");
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

  float val = 0.0f;
  if (!parsePlainFloat(valueText, &val)) return;

  lastRx = millis();
  linkAlive = true;

  bool handled = true;

  if      (!strcmp(key, "T"))    setTilt(val);
  else if (!strcmp(key, "E"))    setTiltError(val);
  else if (!strcmp(key, "A"))    { /* actual twist not drawn on classic screen */ }
  else if (!strcmp(key, "TIP"))  setTip(val);
  else if (!strcmp(key, "P"))    setTip(val);
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

  if (encCount != lastEnc)
  {
    lastEnc = encCount;
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
  splash.loadFont(GEM_ICON);
  splash.createSprite(200, 200);
  splash.fillSprite(TFT_BLACK);
  splash.setTextColor(TFT_WHITE);
  splash.setTextDatum(MC_DATUM);
  splash.drawString("m", 100, 100);
  splash.pushSprite(60, 200);
  splash.unloadFont();

  delay(1200);

  TFT_eSprite logo = TFT_eSprite(&tft);
  logo.loadFont(LOGO);
  logo.createSprite(320, 50);
  logo.fillSprite(TFT_BLACK);
  logo.setTextColor(TFT_WHITE);
  logo.setTextDatum(MC_DATUM);
  logo.drawString("FACET HOUND", 160, 25);
  logo.pushSprite(0, 100);
  logo.unloadFont();

  delay(1200);
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
    unitLabels.drawString("°", 0, 230);
    unitLabels.setTextColor(0xF8F4);
    unitLabels.drawString("mm", 0, 368);
    unitLabels.pushSprite(290, 0);

    axisLabels.createSprite(25, 370);
    axisLabels.setTextDatum(BR_DATUM);
    axisLabels.loadFont(LABELS);
    axisLabels.setTextColor(0x07FE);
    axisLabels.drawString("Φ", 22, 50);
    axisLabels.setTextColor(0xEF00);
    axisLabels.drawString("Θ", 22, 270);
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
    stext2.createSprite(256, 60);
    stext2.loadFont(NUMBERS);
    stext2.setTextColor(0xEF00);
    stext2.setTextDatum(BR_DATUM);

    stext3.setColorDepth(8);
    stext3.createSprite(256, 110);
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
    sBar1.createSprite(300, 20);

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
  stext2.drawFloat(tipAngle, 2, 256, 75);
  stext2.pushSprite(30, 210);
}

void updateZSprite()
{
  stext3.fillSprite(SPRITE_FILL);
  stext3.drawFloat(zValue, 3, 256, 110);
  stext3.pushSprite(30, 270);
}

void updateRPMSprite()
{
  stext4.fillSprite(SPRITE_FILL);
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
    case 0: stext10.drawString("L", 20, 30); break;
    case 1: stext10.drawString("r", 20, 30); break;
    case 2: stext10.drawString("R", 20, 30); break;
    case 3: stext10.drawString("l", 20, 30); break;
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
  markSprite.fillSprite(SPRITE_FILL);
  markSprite.drawFloat(markL, 2, 80, 30);
  markSprite.drawLine(105, 7, 99, 15, 0x07FE);
  markSprite.drawLine(99, 15, 105, 23, 0x07FE);
  markSprite.drawLine(106, 7, 100, 15, 0x07FE);
  markSprite.drawLine(100, 15, 106, 23, 0x07FE);
  markSprite.drawNumber(markIdx, 145, 30);
  markSprite.drawString("/", 158, 30);
  markSprite.drawNumber(nMarkIdx, 190, 30);
  markSprite.drawLine(210, 7, 216, 15, 0x07FE);
  markSprite.drawLine(216, 15, 210, 23, 0x07FE);
  markSprite.drawLine(211, 7, 217, 15, 0x07FE);
  markSprite.drawLine(217, 15, 211, 23, 0x07FE);
  markSprite.drawFloat(markR, 2, 300, 30);
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
  state.twistError = tiltSetError;
  state.tipDegrees = tipAngle;
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
  state.jobActive = jobActive;
  state.jobTitle = jobTitle;
  state.jobFacetName = jobFacetName;
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

  if (displayMode == DisplayMode::CLASSIC)
  {
    tft.fillScreen(TFT_BLACK);
    unitLabels.pushSprite(290, 0);
    axisLabels.pushSprite(0, 0);
    downLabels.pushSprite(0, 450);
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

  EEPROM.begin(256);
  displayMode = loadDisplayMode();

  tft.init();
  tft.setRotation(2);
  drawSplashScreen();

  if (displayMode == DisplayMode::CLASSIC)
  {
    drawMainScreen(true);
  }
  else
  {
    gemUi.begin(displayMode);
    gemUi.tick(currentGemTelemetry());
  }

  encoder.begin();
  delay(50);

  systemReady = true;
  sendLineBoth("@CFGGET,MESH");
}

void loop()
{
  rxUpdate();
  sendEncoder();

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

  if (displayMode == DisplayMode::CLASSIC)
    spriteTickHandler();
  else
    gemUi.tick(currentGemTelemetry());

  yield();
}
