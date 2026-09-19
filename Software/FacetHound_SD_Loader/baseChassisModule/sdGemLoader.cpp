#include "sdGemLoader.h"

#include <SD.h>
#include <SPI.h>
#include <SoftwareSPI.h>
#include <math.h>
#include <strings.h>
#include <utility>

namespace
{
bool sdReady = false;
GemSdDiagnostics sdDiagnostics;
SoftwareSPI gemSdSpi(GEM_SD_SCK_PIN, GEM_SD_MISO_PIN, GEM_SD_MOSI_PIN);
constexpr uint32_t SD_RETRY_INTERVAL_MS = 1000;

bool supportedName(const char* name)
{
    if (!name) return false;
    const char* dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".asc") || !strcasecmp(dot, ".fct"));
}

const char* leafName(const char* path)
{
    if (!path) return "";
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool copyText(char* output, size_t outputSize, const char* input)
{
    if (!output || !outputSize || !input) return false;
    snprintf(output, outputSize, "%s", input);
    return strlen(input) < outputSize;
}

bool reopenCard(bool force = false)
{
    if (sdReady) return true;
    const uint32_t now = millis();
    if (!force && sdDiagnostics.beginAttempts &&
        now - sdDiagnostics.lastAttemptMs < SD_RETRY_INTERVAL_MS)
        return false;

    digitalWrite(GEM_SD_CS_PIN, HIGH);
    sdDiagnostics.beginAttempts++;
    sdDiagnostics.lastAttemptMs = now;
    // Conservative transfer rate for the cabled v9 reader, rather than the
    // library's faster default. Does not format or modify the card.
    SD.end(false);
    sdReady = SD.begin(GEM_SD_CS_PIN, uint32_t(1000000), gemSdSpi);
    sdDiagnostics.ready = sdReady;
    sdDiagnostics.rootChecked = false;
    sdDiagnostics.rootReadable = false;
    if (sdReady) sdDiagnostics.beginSuccesses++;
    return sdReady;
}

File openRoot()
{
    if (!reopenCard()) return File();
    File root = SD.open("/");
    sdDiagnostics.rootChecked = true;
    sdDiagnostics.rootReadable = bool(root) && root.isDirectory();
    if (!sdDiagnostics.rootReadable && root) root.close();
    return root;
}

bool findFile(size_t wanted, char* path, size_t pathSize)
{
    File root = openRoot();
    if (!root || !root.isDirectory()) return false;

    size_t found = 0;
    for (File entry = root.openNextFile(); entry; entry = root.openNextFile())
    {
        if (!entry.isDirectory() && supportedName(entry.name()))
        {
            if (found == wanted)
            {
                const char* name = entry.name();
                bool ok = name[0] == '/'
                              ? copyText(path, pathSize, name)
                              : snprintf(path, pathSize, "/%s", name) < int(pathSize);
                entry.close();
                root.close();
                return ok;
            }
            ++found;
        }
        entry.close();
    }
    root.close();
    return false;
}

char* skipSpace(char* text)
{
    while (text && (*text == ' ' || *text == '\t')) ++text;
    return text;
}

char* nextToken(char** cursor)
{
    if (!cursor || !*cursor) return nullptr;
    char* token = skipSpace(*cursor);
    if (!*token)
    {
        *cursor = token;
        return nullptr;
    }
    char* end = token;
    while (*end && *end != ' ' && *end != '\t') ++end;
    if (*end) *end++ = '\0';
    *cursor = end;
    return token;
}

bool parseFloatToken(const char* text, double* output)
{
    if (!text || !output) return false;
    char* end = nullptr;
    double value = strtod(text, &end);
    if (end == text || *end || !isfinite(value)) return false;
    *output = value;
    return true;
}

bool parseUnsignedToken(const char* text, uint16_t* output)
{
    if (!text || !output) return false;
    char* end = nullptr;
    unsigned long value = strtoul(text, &end, 10);
    if (end == text || *end || value > 65535) return false;
    *output = uint16_t(value);
    return true;
}

GemSdResult appendAscFacets(char* cursor, GemSdDesign* design,
                            double angle, double gemcadDistance, uint16_t tier,
                            uint16_t* facetNumber)
{
    GemCutCoordinate* previous =
        (*facetNumber && !design->cuts.empty() && design->cuts.back().tier == tier)
            ? &design->cuts.back() : nullptr;
    bool facetNameNext = false;

    while (char* token = nextToken(&cursor))
    {
        if (facetNameNext)
        {
            if (previous) copyText(previous->name, sizeof(previous->name), token);
            facetNameNext = false;
            continue;
        }
        if (!strcmp(token, "n"))
        {
            facetNameNext = true;
            continue;
        }
        if (!strcmp(token, "G")) break; // remainder is cutting instructions

        double index = 0.0;
        if (!parseFloatToken(token, &index)) return GemSdResult::BAD_NUMBER;
        if (design->cuts.size() >= GEM_SD_MAX_CUTS)
            return GemSdResult::TOO_MANY_CUTS;

        GemCutCoordinate coordinate;
        coordinate.angleDegrees = angle;
        coordinate.gemcadDistance = gemcadDistance;
        coordinate.index = index;
        coordinate.tier = tier;
        coordinate.facet = ++(*facetNumber);
        design->cuts.push_back(coordinate);
        previous = &design->cuts.back();
    }
    return GemSdResult::OK;
}

GemSdResult parseAscLine(char* line, GemSdDesign* design, bool* headerSeen,
                         double* lastAngle, double* lastGemcadDistance,
                         uint16_t* lastTier, uint16_t* lastFacet)
{
    const bool continuation = line[0] == ' ' || line[0] == '\t';
    char* text = skipSpace(line);
    if (!*text) return GemSdResult::OK;

    if (!*headerSeen)
    {
        if (strncmp(text, "GemCad ", 7)) return GemSdResult::BAD_HEADER;
        *headerSeen = true;
        return GemSdResult::OK;
    }

    if (continuation && *lastTier)
        return appendAscFacets(text, design, *lastAngle, *lastGemcadDistance,
                               *lastTier, lastFacet);

    char record = *text;
    char* cursor = text + 1;
    if (record == 'g')
    {
        char* gearText = nextToken(&cursor);
        char* end = nullptr;
        long signedGear = gearText ? strtol(gearText, &end, 10) : 0;
        if (!gearText || end == gearText || *end || signedGear == 0 ||
            labs(signedGear) > 400)
            return GemSdResult::BAD_NUMBER;
        design->designIndexSign = signedGear < 0 ? -1 : 1;
        design->wheelIndex = double(labs(signedGear));
        char* meridianText = nextToken(&cursor);
        if (meridianText && !parseFloatToken(meridianText, &design->meridian))
            return GemSdResult::BAD_NUMBER;
    }
    else if (record == 'H' && !design->title[0])
    {
        copyText(design->title, sizeof(design->title), skipSpace(cursor));
    }
    else if (record == 'a')
    {
        char* angleText = nextToken(&cursor);
        char* distanceText = nextToken(&cursor);
        if (!parseFloatToken(angleText, lastAngle) ||
            !parseFloatToken(distanceText, lastGemcadDistance))
            return GemSdResult::BAD_NUMBER;
        *lastTier = ++design->tierCount;
        *lastFacet = 0;
        return appendAscFacets(cursor, design, *lastAngle, *lastGemcadDistance,
                               *lastTier, lastFacet);
    }
    return GemSdResult::OK;
}

GemSdResult parseFctLine(char* line, GemSdDesign* design, bool* headerSeen)
{
    char* text = skipSpace(line);
    if (!*text || *text == '#') return GemSdResult::OK;
    if (!*headerSeen)
    {
        if (strcmp(text, "FacetHound 1")) return GemSdResult::BAD_HEADER;
        *headerSeen = true;
        return GemSdResult::OK;
    }

    char record = *text;
    char* cursor = text + 1;
    if (record == 'g')
    {
        char* gear = nextToken(&cursor);
        if (!parseFloatToken(gear, &design->wheelIndex) ||
            design->wheelIndex <= 0.0f || design->wheelIndex > 400.0f)
            return GemSdResult::BAD_NUMBER;
        char* meridian = nextToken(&cursor);
        if (meridian && !parseFloatToken(meridian, &design->meridian))
            return GemSdResult::BAD_NUMBER;
    }
    else if (record == 'H' && !design->title[0])
    {
        copyText(design->title, sizeof(design->title), skipSpace(cursor));
    }
    else if (record == 'c')
    {
        if (design->cuts.size() >= GEM_SD_MAX_CUTS)
            return GemSdResult::TOO_MANY_CUTS;
        char* tier = nextToken(&cursor);
        char* facet = nextToken(&cursor);
        char* angle = nextToken(&cursor);
        char* gemcadDistance = nextToken(&cursor);
        char* index = nextToken(&cursor);
        GemCutCoordinate coordinate;
        if (!parseUnsignedToken(tier, &coordinate.tier) ||
            !parseUnsignedToken(facet, &coordinate.facet) ||
            !parseFloatToken(angle, &coordinate.angleDegrees) ||
            !parseFloatToken(gemcadDistance, &coordinate.gemcadDistance) ||
            !parseFloatToken(index, &coordinate.index) ||
            coordinate.tier == 0 || coordinate.facet == 0)
            return GemSdResult::BAD_NUMBER;
        char* name = nextToken(&cursor);
        if (name) copyText(coordinate.name, sizeof(coordinate.name), name);
        design->cuts.push_back(coordinate);
        if (coordinate.tier > design->tierCount)
            design->tierCount = coordinate.tier;
    }
    return GemSdResult::OK;
}

void propagateTierNames(GemSdDesign* design)
{
    // GemCad writes the tier label after one index with `n <name>`. Facet Hound
    // presents names as tier labels for either supported file type, so copy the
    // first name found in each tier to all cuts in that tier.
    for (uint16_t tier = 1; tier <= design->tierCount; ++tier)
    {
        char tierName[GEM_SD_FACET_NAME_LENGTH] = {};
        for (const GemCutCoordinate& cut : design->cuts)
        {
            if (cut.tier == tier && cut.name[0])
            {
                copyText(tierName, sizeof(tierName), cut.name);
                break;
            }
        }
        if (!tierName[0]) continue;
        for (GemCutCoordinate& cut : design->cuts)
            if (cut.tier == tier)
                copyText(cut.name, sizeof(cut.name), tierName);
    }
}

GemSdResult parseDesign(File& file, bool fct, GemSdDesign* design)
{
    constexpr size_t lineSize = 384;
    char line[lineSize] = {};
    size_t length = 0;
    bool overflow = false;
    bool headerSeen = false;
    double lastAngle = 0.0;
    double lastGemcadDistance = 0.0;
    uint16_t lastTier = 0;
    uint16_t lastFacet = 0;

    while (file.available())
    {
        char c = char(file.read());
        if (c == '\r') continue;
        if (c != '\n')
        {
            if (length + 1 < lineSize) line[length++] = c;
            else overflow = true;
            continue;
        }
        if (overflow) return GemSdResult::LINE_TOO_LONG;
        line[length] = '\0';
        GemSdResult result = fct
            ? parseFctLine(line, design, &headerSeen)
            : parseAscLine(line, design, &headerSeen, &lastAngle, &lastGemcadDistance,
                           &lastTier, &lastFacet);
        if (result != GemSdResult::OK) return result;
        length = 0;
    }

    if (overflow) return GemSdResult::LINE_TOO_LONG;
    if (length)
    {
        line[length] = '\0';
        GemSdResult result = fct
            ? parseFctLine(line, design, &headerSeen)
            : parseAscLine(line, design, &headerSeen, &lastAngle, &lastGemcadDistance,
                           &lastTier, &lastFacet);
        if (result != GemSdResult::OK) return result;
    }
    if (!headerSeen) return GemSdResult::BAD_HEADER;
    if (design->cuts.empty()) return GemSdResult::NO_CUTS;
    propagateTierNames(design);
    return GemSdResult::OK;
}
} // namespace

bool beginGemSd()
{
    pinMode(GEM_SD_CS_PIN, OUTPUT);
    digitalWrite(GEM_SD_CS_PIN, HIGH);
    gemSdSpi.begin();
    sdReady = false;
    sdDiagnostics = GemSdDiagnostics{};
    return reopenCard(true);
}

// A status query must never initialize hardware. SD.begin() can block for
// seconds with no card, starving UART consumers and STEP generation whenever
// STATUS, STREAM or a settings snapshot asks whether the card is ready.
// Initialization remains explicit at boot, SD RETRY, and file operations.
bool gemSdReady() { return sdReady; }

bool retryGemSd()
{
    sdReady = false;
    sdDiagnostics.ready = false;
    return reopenCard(true);
}

const GemSdDiagnostics& gemSdDiagnostics()
{
    sdDiagnostics.ready = sdReady;
    return sdDiagnostics;
}

size_t gemSdFileCount()
{
    File root = openRoot();
    if (!root || !root.isDirectory()) return 0;
    size_t count = 0;
    for (File entry = root.openNextFile(); entry; entry = root.openNextFile())
    {
        if (!entry.isDirectory() && supportedName(entry.name())) ++count;
        entry.close();
    }
    root.close();
    sdDiagnostics.lastFileCount = count;
    return count;
}

bool gemSdFileNameAt(size_t index, char* output, size_t outputSize)
{
    char path[GEM_SD_FILE_NAME_LENGTH] = {};
    if (!findFile(index, path, sizeof(path))) return false;
    return copyText(output, outputSize, leafName(path));
}

bool gemSdFilePathAt(size_t index, char* output, size_t outputSize)
{
    return findFile(index, output, outputSize);
}

GemSdResult loadGemSdFileAt(size_t index, GemSdDesign* design)
{
    if (!design) return GemSdResult::OPEN_FAILED;
    if (!reopenCard()) return GemSdResult::NO_CARD;

    char path[GEM_SD_FILE_NAME_LENGTH] = {};
    if (!findFile(index, path, sizeof(path))) return GemSdResult::OPEN_FAILED;
    File file = SD.open(path, FILE_READ);
    if (!file) return GemSdResult::OPEN_FAILED;

    GemSdDesign candidate;
    copyText(candidate.fileName, sizeof(candidate.fileName), leafName(path));
    const char* dot = strrchr(path, '.');
    GemSdResult result = parseDesign(file, dot && !strcasecmp(dot, ".fct"), &candidate);
    file.close();
    if (result != GemSdResult::OK) return result;
    if (!candidate.title[0])
        copyText(candidate.title, sizeof(candidate.title), candidate.fileName);
    *design = std::move(candidate);
    return GemSdResult::OK;
}

const char* gemSdResultText(GemSdResult result)
{
    switch (result)
    {
        case GemSdResult::OK:               return "OK";
        case GemSdResult::NO_CARD:          return "NO_CARD";
        case GemSdResult::OPEN_FAILED:      return "OPEN_FAILED";
        case GemSdResult::UNSUPPORTED_FILE: return "UNSUPPORTED_FILE";
        case GemSdResult::BAD_HEADER:       return "BAD_HEADER";
        case GemSdResult::BAD_NUMBER:       return "BAD_NUMBER";
        case GemSdResult::NO_CUTS:          return "NO_CUTS";
        case GemSdResult::TOO_MANY_CUTS:    return "TOO_MANY_CUTS";
        case GemSdResult::LINE_TOO_LONG:    return "LINE_TOO_LONG";
        default:                            return "UNKNOWN";
    }
}
