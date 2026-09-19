#pragma once

#include <Arduino.h>
#include <vector>

// BaseModule v9 microSD header. This order is implemented with the
// bounded GPIO SPI transport because it is not a legal
// hardware-SPI signal mapping.
#define GEM_SD_CS_PIN   10
#define GEM_SD_SCK_PIN  12
#define GEM_SD_MOSI_PIN 11
#define GEM_SD_MISO_PIN 13

constexpr size_t GEM_SD_MAX_CUTS = 2048;
constexpr size_t GEM_SD_FILE_NAME_LENGTH = 64;
constexpr size_t GEM_SD_TITLE_LENGTH = 48;
constexpr size_t GEM_SD_FACET_NAME_LENGTH = 12;

struct GemCutCoordinate
{
    double angleDegrees = 0.0;
    double gemcadDistance = 0.0; // center-to-facet plane distance; never machine Z
    double index = 0.0;
    uint16_t tier = 0;   // one based, in GemCad cutting order
    uint16_t facet = 0;  // one based within the tier
    char name[GEM_SD_FACET_NAME_LENGTH] = {};
};

struct GemSdDesign
{
    double wheelIndex = 96.0;
    double meridian = 0.0;
    int8_t designIndexSign = 1;
    uint16_t tierCount = 0;
    char title[GEM_SD_TITLE_LENGTH] = {};
    char fileName[GEM_SD_FILE_NAME_LENGTH] = {};
    std::vector<GemCutCoordinate> cuts;
};

enum class GemSdResult : uint8_t
{
    OK,
    NO_CARD,
    OPEN_FAILED,
    UNSUPPORTED_FILE,
    BAD_HEADER,
    BAD_NUMBER,
    NO_CUTS,
    TOO_MANY_CUTS,
    LINE_TOO_LONG,
};

struct GemSdDiagnostics
{
    bool ready = false;
    bool rootChecked = false;
    bool rootReadable = false;
    uint32_t beginAttempts = 0;
    uint32_t beginSuccesses = 0;
    uint32_t lastAttemptMs = 0;
    size_t lastFileCount = 0;
};

bool beginGemSd();
bool gemSdReady();
bool retryGemSd();
uint8_t probeGemSdCommand0();
void probeGemSdInitialization();
void probeGemSdFilesystem();
const GemSdDiagnostics& gemSdDiagnostics();
size_t gemSdFileCount();
bool gemSdFileNameAt(size_t index, char* output, size_t outputSize);
bool gemSdFilePathAt(size_t index, char* output, size_t outputSize);
GemSdResult loadGemSdFileAt(size_t index, GemSdDesign* design);
const char* gemSdResultText(GemSdResult result);
