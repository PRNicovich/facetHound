#include "sdGemLoader.h"

#include <SD.h>
#include <SPI.h>
#include "GemSdSpi.h"
#include <SdFat.h>
#include <math.h>
#include <strings.h>
#include <utility>

namespace
{
bool sdReady = false;
GemSdDiagnostics sdDiagnostics;
GemSdSpi gemSdSpi(GEM_SD_SCK_PIN, GEM_SD_MISO_PIN, GEM_SD_MOSI_PIN);
constexpr uint32_t SD_RETRY_INTERVAL_MS = 1000;

// Keep the installed SDFS/SdFat file API and explicitly close the underlying
// volume between attempts. Shared mode terminates reads between operations.
class GemCardFilesystem : public sdfs::SDFSImpl {
public:
    bool begin() override {
        if (_mounted) return true;
        _mounted = _fs.begin(SdSpiConfig(_cfg._csPin, SHARED_SPI,
                                        _cfg._spiSettings, _cfg._spi));
        Serial.print("@SD_MOUNT,bus=gpio,ready="); Serial.print(_mounted ? 1 : 0);
        Serial.print(",error=0x"); Serial.print(_fs.sdErrorCode(), HEX);
        Serial.print(",data=0x"); Serial.println(_fs.sdErrorData(), HEX);
        FsDateTime::setCallback(dateTimeCB);
        return _mounted;
    }
    void end() override {
        _fs.end();
        _mounted = false;
    }
};

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
    sdReady = SD.begin(GEM_SD_CS_PIN, uint32_t(250000), gemSdSpi);
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
        if (!strcmp(token, "G")) {
            if(design->tierComments.size()<=tier) design->tierComments.resize(tier+1);
            String comment(skipSpace(cursor));
            comment.trim();
            String& saved=design->tierComments[tier];
            if(saved.length() && comment.length()) saved += " ";
            saved += comment;
            if(saved.length()>96) saved.remove(96);
            break;
        }

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
        snprintf(design->formatVersion, sizeof(design->formatVersion), "%s", text+7);
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
    else if (record == 'y')
    {
        char* fold = nextToken(&cursor); char* mirrored = nextToken(&cursor);
        design->symmetry = fold ? atoi(fold) : 0;
        design->mirror = mirrored && *mirrored=='y';
    }
    else if (record == 'I')
        design->refractiveIndex = strtod(skipSpace(cursor), nullptr);
    else if (record == 'H' && design->title[0])
    {
        size_t used = strlen(design->attribution);
        snprintf(design->attribution+used, sizeof(design->attribution)-used,
                 "%s%s", used ? "; " : "", skipSpace(cursor));
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
    SDFS = fs::FS(std::make_shared<GemCardFilesystem>());
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

uint8_t probeGemSdCommand0()
{
    // Explicit electrical/SPI diagnostic, not a filesystem or format operation.
    // Reset the card to idle; caller must use SD RETRY before loading a file.
    SD.end(false);
    sdReady = false;
    sdDiagnostics.ready = false;
    sdDiagnostics.rootChecked = false;
    digitalWrite(GEM_SD_CS_PIN, HIGH);
    gemSdSpi.begin(); // Idempotent; no state machines or heap allocation.
    gemSdSpi.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));
    for (uint8_t i = 0; i < 10; ++i) gemSdSpi.transfer(uint8_t(0xFF));
    digitalWrite(GEM_SD_CS_PIN, LOW);
    const uint8_t command[6] = {0x40, 0, 0, 0, 0, 0x95};
    for (uint8_t b : command) gemSdSpi.transfer(b);
    uint8_t response = 0xFF;
    for (uint8_t i = 0; i < 16; ++i) {
        response = gemSdSpi.transfer(uint8_t(0xFF));
        if (!(response & 0x80)) break;
    }
    digitalWrite(GEM_SD_CS_PIN, HIGH);
    gemSdSpi.transfer(uint8_t(0xFF));
    gemSdSpi.endTransaction();
    return response;
}

void probeGemSdInitialization()
{
    // Called only after CMD0 succeeded, with motion stopped. This does not
    // access sectors or write files. Bound the ready polling to one second.
    auto command = [](uint8_t cmd, uint32_t arg, uint8_t crc,
                      uint8_t* tail, uint8_t tailSize) -> uint8_t {
        digitalWrite(GEM_SD_CS_PIN, HIGH);
        gemSdSpi.transfer(uint8_t(0xFF));
        digitalWrite(GEM_SD_CS_PIN, LOW);
        gemSdSpi.transfer(uint8_t(0xFF));
        gemSdSpi.transfer(uint8_t(0x40 | cmd));
        for (int shift = 24; shift >= 0; shift -= 8)
            gemSdSpi.transfer(uint8_t(arg >> shift));
        gemSdSpi.transfer(crc);
        uint8_t r1 = 0xFF;
        for (uint8_t i = 0; i < 16; ++i) {
            r1 = gemSdSpi.transfer(uint8_t(0xFF));
            if (!(r1 & 0x80)) break;
        }
        if (r1 <= 1)
            for (uint8_t i = 0; i < tailSize; ++i)
                tail[i] = gemSdSpi.transfer(uint8_t(0xFF));
        digitalWrite(GEM_SD_CS_PIN, HIGH);
        gemSdSpi.transfer(uint8_t(0xFF));
        return r1;
    };
    gemSdSpi.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));
    uint8_t r7[4] = {};
    const uint8_t cmd8 = command(8, 0x1AA, 0x87, r7, 4);
    Serial.print("@SD_INIT,cmd8_r1=0x"); Serial.print(cmd8, HEX);
    Serial.print(",r7=");
    for (uint8_t b : r7) { if (b < 16) Serial.print('0'); Serial.print(b, HEX); }
    Serial.println();
    const bool v2 = cmd8 == 1 && r7[2] == 1 && r7[3] == 0xAA;
    if (!v2 && cmd8 != 5) {
        Serial.println("@SD_INIT,stage=CMD8_FAILED,next=SD RETRY");
        gemSdSpi.endTransaction();
        return;
    }
    uint8_t cmd55 = 0xFF, acmd41 = 0xFF;
    const uint32_t started = millis();
    do {
        cmd55 = command(55, 0, 1, nullptr, 0);
        if (cmd55 > 1) break;
        acmd41 = command(41, v2 ? 0x40000000UL : 0, 1, nullptr, 0);
        if (acmd41 != 1) break;
        delay(1);
    } while (millis() - started < 1000);
    Serial.print("@SD_INIT,cmd55_r1=0x"); Serial.print(cmd55, HEX);
    Serial.print(",acmd41_r1=0x"); Serial.print(acmd41, HEX);
    Serial.print(",elapsed_ms="); Serial.println(millis() - started);
    if (cmd55 <= 1 && acmd41 == 0) {
        uint8_t ocr[4] = {};
        const uint8_t cmd58 = command(58, 0, 1, ocr, 4);
        Serial.print("@SD_INIT,cmd58_r1=0x"); Serial.print(cmd58, HEX);
        Serial.print(",ocr=");
        for (uint8_t b : ocr) { if (b < 16) Serial.print('0'); Serial.print(b, HEX); }
        Serial.println(",next=SD RETRY");
    }
    gemSdSpi.endTransaction();
}

void probeGemSdFilesystem()
{
    // Use SdFat's standard SdInfo sequence instead of another custom SD driver.
    // All accesses are reads; this diagnostic never formats or mounts for writing.
    SD.end(false);
    sdReady = false;
    sdDiagnostics.ready = false;
    sdDiagnostics.rootChecked = false;
    SdFat probe;
    const bool card = probe.cardBegin(SdSpiConfig(
        GEM_SD_CS_PIN, SHARED_SPI, uint32_t(250000), &gemSdSpi));
    Serial.print("@SD_FS,card_init="); Serial.print(card ? "OK" : "FAIL");
    Serial.print(",error=0x"); Serial.print(probe.sdErrorCode(), HEX);
    Serial.print(",data=0x"); Serial.println(probe.sdErrorData(), HEX);
    if (card) {
        uint8_t sector[512];
        const bool read = probe.card()->readSector(0, sector);
        Serial.print("@SD_FS,sector0="); Serial.print(read ? "OK" : "FAIL");
        Serial.print(",error=0x"); Serial.print(probe.sdErrorCode(), HEX);
        Serial.print(",data=0x"); Serial.print(probe.sdErrorData(), HEX);
        if (read) {
            Serial.print(",signature=");
            Serial.print(sector[510], HEX); Serial.print('-'); Serial.print(sector[511], HEX);
            Serial.print(",partition_types=");
            for (uint8_t i = 0; i < 4; ++i) {
                if (i) Serial.print('-');
                Serial.print(sector[446 + i * 16 + 4], HEX);
            }
        }
        Serial.println();
        if (read) {
            const bool volume = probe.volumeBegin();
            Serial.print("@SD_FS,volume="); Serial.print(volume ? "OK" : "FAIL");
            Serial.print(",fat_type="); Serial.print(volume ? probe.fatType() : 0);
            Serial.print(",error=0x"); Serial.print(probe.sdErrorCode(), HEX);
            Serial.print(",data=0x"); Serial.println(probe.sdErrorData(), HEX);
        }
    }
    probe.end();
    Serial.println("@SD_FS,next=SD RETRY");
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

bool gemSdFileTitleAt(size_t index, char* title, size_t size)
{
    if (!title || !size) return false;
    snprintf(title, size, "null");
    char path[GEM_SD_FILE_NAME_LENGTH] = {};
    if (!findFile(index, path, sizeof(path))) return false;
    File file = SD.open(path, FILE_READ);
    if (!file) return false;
    bool header = false;
    char line[192];
    // Bounded metadata preview: validate header, then find the first title.
    for (int row = 0; row < 24 && file.available(); ++row) {
        size_t n = file.readBytesUntil('\n', line, sizeof(line)-1); line[n]=0;
        char* text = skipSpace(line);
        size_t len = strlen(text);
        while (len && (text[len-1]=='\r' || text[len-1]==' ')) text[--len]=0;
        if (!*text) continue;
        if (!header) {
            header = !strncmp(text,"GemCad ",7) || !strncmp(text,"FacetHound 1",12);
            if (!header) break;
        } else if (*text=='H') {
            snprintf(title, size, "%s", skipSpace(text+1)); file.close(); return true;
        }
    }
    file.close(); return header;
}

bool readGemSdMetadataAt(size_t index, GemSdDesign* design)
{
    char path[GEM_SD_FILE_NAME_LENGTH] = {};
    if (!design || !findFile(index,path,sizeof(path))) return false;
    File file = SD.open(path, FILE_READ); if (!file) return false;
    GemSdDesign header; bool seen = false; double angle=0, distance=0;
    uint16_t tier=0, facet=0;
    char line[384];
    for (int row=0; row<32 && file.available(); ++row) {
        size_t n=file.readBytesUntil('\n',line,sizeof(line)-1); line[n]=0;
        if (n && line[n-1]=='\r') line[n-1]=0;
        if (*skipSpace(line)=='a') break;
        if (parseAscLine(line,&header,&seen,&angle,&distance,&tier,&facet)!=GemSdResult::OK) break;
    }
    file.close();
    if (!seen) return false;
    design->symmetry=header.symmetry; design->mirror=header.mirror;
    design->refractiveIndex=header.refractiveIndex;
    snprintf(design->formatVersion,sizeof(design->formatVersion),"%s",header.formatVersion);
    snprintf(design->attribution,sizeof(design->attribution),"%s",header.attribution);
    return true;
}

bool rememberGemSdPath(const char* path)
{
    if (!sdReady || !path || strlen(path) >= GEM_SD_FILE_NAME_LENGTH) return false;
    File file = SD.open("/facetHound.last", "w");
    if (!file) return false;
    bool ok = file.println(path) == strlen(path)+2;
    file.close(); return ok;
}

int lastGemSdIndex()
{
    if (!sdReady) return -1;
    File file = SD.open("/facetHound.last", FILE_READ);
    if (!file) return -1;
    char wanted[GEM_SD_FILE_NAME_LENGTH] = {};
    size_t n = file.readBytesUntil('\n', wanted, sizeof(wanted)-1); file.close();
    while (n && (wanted[n-1]=='\r' || wanted[n-1]=='\n')) wanted[--n]=0;
    const size_t count = gemSdFileCount();
    for (size_t i=0; i<count; ++i) {
        char path[GEM_SD_FILE_NAME_LENGTH] = {};
        if (findFile(i,path,sizeof(path)) && !strcmp(path,wanted)) return int(i);
    }
    return -2; // A remembered selection exists but cannot be restored.
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
