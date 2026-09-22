#include "gemCache.h"

#include <SD.h>
#include <string.h>
#include <utility>

namespace
{
constexpr char kMagic[8] = {'F', 'H', 'C', 'A', 'C', 'H', 'E', '1'};
constexpr uint16_t kVersion = 2; // Rebuild caches made before the culet-plane fix.

struct __attribute__((packed)) CacheHeader
{
    char magic[8];
    uint16_t version;
    uint16_t headerSize;
    uint32_t sourceSize;
    uint32_t sourceCrc32;
    double wheelIndex;
    double meridian;
    int8_t designIndexSign;
    uint8_t reserved0;
    uint16_t tierCount;
    uint32_t cutCount;
    uint16_t vertexCount;
    uint16_t edgeCount;
    uint16_t planeCount;
    uint16_t reserved1;
    float radius;
    char title[GEM_SD_TITLE_LENGTH];
    char sourceName[GEM_SD_FILE_NAME_LENGTH];
};

struct __attribute__((packed)) CacheCut
{
    double angleDegrees;
    double gemcadDistance;
    double index;
    uint16_t tier;
    uint16_t facet;
    char name[GEM_SD_FACET_NAME_LENGTH];
};

struct __attribute__((packed)) CacheVertex
{
    double x;
    double y;
    double z;
};

struct __attribute__((packed)) CacheEdge
{
    uint16_t a;
    uint16_t b;
    uint8_t supportCount;
    uint16_t supportPlanes[GEM_GEOMETRY_MAX_EDGE_SUPPORTS];
};

struct __attribute__((packed)) CachePlane
{
    double nx;
    double ny;
    double nz;
    double distance;
    float angleDegrees;
    float index;
    uint16_t tier;
    uint16_t facet;
    char name[GEM_SD_FACET_NAME_LENGTH];
};

static_assert(sizeof(CacheHeader) == 168, "Facet Hound cache header layout changed");
static_assert(sizeof(CacheCut) == 40, "Facet Hound cache cut layout changed");
static_assert(sizeof(CacheVertex) == 24, "Facet Hound cache vertex layout changed");
static_assert(sizeof(CacheEdge) == 21, "Facet Hound cache edge layout changed");
static_assert(sizeof(CachePlane) == 56, "Facet Hound cache plane layout changed");

const char* leafName(const char* path)
{
    const char* slash = path ? strrchr(path, '/') : nullptr;
    return slash ? slash + 1 : (path ? path : "");
}

bool cachePathForSource(const char* sourcePath, char* output, size_t outputSize)
{
    if (!sourcePath || !output || outputSize < 6) return false;
    const char* dot = strrchr(sourcePath, '.');
    size_t baseLength = dot ? size_t(dot - sourcePath) : strlen(sourcePath);
    if (baseLength + 5 > outputSize) return false;
    memcpy(output, sourcePath, baseLength);
    memcpy(output + baseLength, ".fhc", 5);
    return true;
}

uint32_t updateCrc32(uint32_t crc, const uint8_t* data, size_t length)
{
    crc = ~crc;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & uint32_t(-int32_t(crc & 1u)));
    }
    return ~crc;
}

bool sourceFingerprint(const char* sourcePath, uint32_t* size, uint32_t* crc)
{
    if (!size || !crc) return false;
    File source = SD.open(sourcePath, FILE_READ);
    if (!source) return false;
    uint8_t buffer[256];
    uint32_t total = 0;
    uint32_t value = 0;
    while (source.available())
    {
        int count = source.read(buffer, sizeof(buffer));
        if (count <= 0) { source.close(); return false; }
        value = updateCrc32(value, buffer, size_t(count));
        total += uint32_t(count);
    }
    source.close();
    *size = total;
    *crc = value;
    return true;
}

bool readExact(File& file, void* output, size_t size)
{
    return file.read(static_cast<uint8_t*>(output), size) == int(size);
}

bool writeExact(File& file, const void* input, size_t size)
{
    return file.write(static_cast<const uint8_t*>(input), size) == size;
}

template <size_t N>
void copyFixed(char (&output)[N], const char* input)
{
    memset(output, 0, N);
    if (input) strncpy(output, input, N - 1);
}

bool validCounts(const CacheHeader& header)
{
    return header.cutCount > 0 && header.cutCount <= GEM_SD_MAX_CUTS &&
           header.planeCount > 0 && header.planeCount <= GEM_GEOMETRY_MAX_PLANES &&
           header.vertexCount > 0 && header.vertexCount <= GEM_GEOMETRY_MAX_VERTICES &&
           header.edgeCount <= GEM_GEOMETRY_MAX_EDGES;
}
} // namespace

GemCacheResult loadGemCache(const char* sourcePath, GemSdDesign* design,
                            GemRuntimeGeometry* geometry)
{
    if (!sourcePath || !design || !geometry) return GemCacheResult::BAD_HEADER;
    char cachePath[GEM_SD_PATH_LENGTH] = {};
    if (!cachePathForSource(sourcePath, cachePath, sizeof(cachePath)))
        return GemCacheResult::CACHE_OPEN_FAILED;
    if (!SD.exists(cachePath)) return GemCacheResult::NOT_FOUND;

    uint32_t sourceSize = 0;
    uint32_t sourceCrc = 0;
    if (!sourceFingerprint(sourcePath, &sourceSize, &sourceCrc))
        return GemCacheResult::SOURCE_OPEN_FAILED;

    File file = SD.open(cachePath, FILE_READ);
    if (!file) return GemCacheResult::CACHE_OPEN_FAILED;
    CacheHeader header = {};
    if (!readExact(file, &header, sizeof(header)))
    {
        file.close();
        return GemCacheResult::SHORT_READ;
    }
    if (memcmp(header.magic, kMagic, sizeof(kMagic)) ||
        header.version != kVersion || header.headerSize != sizeof(CacheHeader))
    {
        file.close();
        return GemCacheResult::BAD_HEADER;
    }
    if (header.sourceSize != sourceSize || header.sourceCrc32 != sourceCrc)
    {
        file.close();
        return GemCacheResult::STALE;
    }
    if (!validCounts(header))
    {
        file.close();
        return GemCacheResult::BAD_COUNTS;
    }

    GemSdDesign candidate;
    candidate.wheelIndex = header.wheelIndex;
    candidate.meridian = header.meridian;
    candidate.designIndexSign = header.designIndexSign;
    candidate.tierCount = header.tierCount;
    copyFixed(candidate.title, header.title);
    copyFixed(candidate.fileName, leafName(sourcePath));
    candidate.cuts.resize(header.cutCount);

    GemRuntimeGeometry candidateGeometry;
    candidateGeometry.radius = header.radius;
    candidateGeometry.vertices.resize(header.vertexCount);
    candidateGeometry.edges.resize(header.edgeCount);
    candidateGeometry.planes.resize(header.planeCount);

    for (GemCutCoordinate& cut : candidate.cuts)
    {
        CacheCut disk = {};
        if (!readExact(file, &disk, sizeof(disk))) { file.close(); return GemCacheResult::SHORT_READ; }
        cut.angleDegrees = disk.angleDegrees;
        cut.gemcadDistance = disk.gemcadDistance;
        cut.index = disk.index;
        cut.tier = disk.tier;
        cut.facet = disk.facet;
        copyFixed(cut.name, disk.name);
    }
    for (GemGeometryVertex& vertex : candidateGeometry.vertices)
    {
        CacheVertex disk = {};
        if (!readExact(file, &disk, sizeof(disk))) { file.close(); return GemCacheResult::SHORT_READ; }
        vertex = {disk.x, disk.y, disk.z};
    }
    for (GemGeometryEdge& edge : candidateGeometry.edges)
    {
        CacheEdge disk = {};
        if (!readExact(file, &disk, sizeof(disk))) { file.close(); return GemCacheResult::SHORT_READ; }
        if (disk.a >= header.vertexCount || disk.b >= header.vertexCount ||
            disk.supportCount > GEM_GEOMETRY_MAX_EDGE_SUPPORTS)
        {
            file.close(); return GemCacheResult::BAD_COUNTS;
        }
        edge.a = disk.a;
        edge.b = disk.b;
        edge.supportCount = disk.supportCount;
        for (uint8_t i = 0; i < edge.supportCount; ++i)
        {
            if (disk.supportPlanes[i] >= header.planeCount)
            {
                file.close(); return GemCacheResult::BAD_COUNTS;
            }
            edge.supportPlanes[i] = disk.supportPlanes[i];
        }
    }
    for (GemGeometryPlane& plane : candidateGeometry.planes)
    {
        CachePlane disk = {};
        if (!readExact(file, &disk, sizeof(disk))) { file.close(); return GemCacheResult::SHORT_READ; }
        plane.nx = disk.nx;
        plane.ny = disk.ny;
        plane.nz = disk.nz;
        plane.distance = disk.distance;
        plane.angleDegrees = disk.angleDegrees;
        plane.index = disk.index;
        plane.tier = disk.tier;
        plane.facet = disk.facet;
        copyFixed(plane.name, disk.name);
    }
    file.close();
    *design = std::move(candidate);
    *geometry = std::move(candidateGeometry);
    return GemCacheResult::OK;
}

GemCacheResult saveGemCache(const char* sourcePath, const GemSdDesign& design,
                            const GemRuntimeGeometry& geometry)
{
    char cachePath[GEM_SD_PATH_LENGTH] = {};
    if (!cachePathForSource(sourcePath, cachePath, sizeof(cachePath)))
        return GemCacheResult::CACHE_OPEN_FAILED;
    uint32_t sourceSize = 0;
    uint32_t sourceCrc = 0;
    if (!sourceFingerprint(sourcePath, &sourceSize, &sourceCrc))
        return GemCacheResult::SOURCE_OPEN_FAILED;
    if (design.cuts.size() > GEM_SD_MAX_CUTS ||
        geometry.vertices.size() > GEM_GEOMETRY_MAX_VERTICES ||
        geometry.edges.size() > GEM_GEOMETRY_MAX_EDGES ||
        geometry.planes.size() > GEM_GEOMETRY_MAX_PLANES)
        return GemCacheResult::BAD_COUNTS;

    CacheHeader header = {};
    memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.headerSize = sizeof(CacheHeader);
    header.sourceSize = sourceSize;
    header.sourceCrc32 = sourceCrc;
    header.wheelIndex = design.wheelIndex;
    header.meridian = design.meridian;
    header.designIndexSign = design.designIndexSign;
    header.tierCount = design.tierCount;
    header.cutCount = design.cuts.size();
    header.vertexCount = geometry.vertices.size();
    header.edgeCount = geometry.edges.size();
    header.planeCount = geometry.planes.size();
    header.radius = geometry.radius;
    copyFixed(header.title, design.title);
    copyFixed(header.sourceName, leafName(sourcePath));

    if (SD.exists(cachePath)) SD.remove(cachePath);
    File file = SD.open(cachePath, FILE_WRITE);
    if (!file) return GemCacheResult::CACHE_OPEN_FAILED;
    bool ok = writeExact(file, &header, sizeof(header));
    for (const GemCutCoordinate& cut : design.cuts)
    {
        CacheCut disk = {};
        disk.angleDegrees = cut.angleDegrees;
        disk.gemcadDistance = cut.gemcadDistance;
        disk.index = cut.index;
        disk.tier = cut.tier;
        disk.facet = cut.facet;
        copyFixed(disk.name, cut.name);
        ok = ok && writeExact(file, &disk, sizeof(disk));
    }
    for (const GemGeometryVertex& vertex : geometry.vertices)
    {
        CacheVertex disk = {vertex.x, vertex.y, vertex.z};
        ok = ok && writeExact(file, &disk, sizeof(disk));
    }
    for (const GemGeometryEdge& edge : geometry.edges)
    {
        CacheEdge disk = {};
        disk.a = edge.a;
        disk.b = edge.b;
        disk.supportCount = edge.supportCount;
        for (uint8_t i = 0; i < edge.supportCount; ++i)
            disk.supportPlanes[i] = edge.supportPlanes[i];
        ok = ok && writeExact(file, &disk, sizeof(disk));
    }
    for (const GemGeometryPlane& plane : geometry.planes)
    {
        CachePlane disk = {};
        disk.nx = plane.nx;
        disk.ny = plane.ny;
        disk.nz = plane.nz;
        disk.distance = plane.distance;
        disk.angleDegrees = plane.angleDegrees;
        disk.index = plane.index;
        disk.tier = plane.tier;
        disk.facet = plane.facet;
        copyFixed(disk.name, plane.name);
        ok = ok && writeExact(file, &disk, sizeof(disk));
    }
    file.flush();
    file.close();
    if (!ok)
    {
        SD.remove(cachePath);
        return GemCacheResult::WRITE_FAILED;
    }
    return GemCacheResult::OK;
}

const char* gemCacheResultText(GemCacheResult result)
{
    switch (result)
    {
        case GemCacheResult::OK:                 return "OK";
        case GemCacheResult::NOT_FOUND:          return "NOT_FOUND";
        case GemCacheResult::SOURCE_OPEN_FAILED: return "SOURCE_OPEN_FAILED";
        case GemCacheResult::CACHE_OPEN_FAILED:  return "CACHE_OPEN_FAILED";
        case GemCacheResult::STALE:              return "STALE";
        case GemCacheResult::BAD_HEADER:         return "BAD_HEADER";
        case GemCacheResult::BAD_COUNTS:         return "BAD_COUNTS";
        case GemCacheResult::SHORT_READ:         return "SHORT_READ";
        case GemCacheResult::WRITE_FAILED:       return "WRITE_FAILED";
        default:                                  return "UNKNOWN";
    }
}
