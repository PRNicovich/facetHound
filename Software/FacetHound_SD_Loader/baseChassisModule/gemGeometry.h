#pragma once

#include <Arduino.h>
#include <vector>

#include "sdGemLoader.h"

constexpr uint16_t GEM_GEOMETRY_MAX_PLANES = 512;
constexpr uint16_t GEM_GEOMETRY_MAX_VERTICES = 1024;
constexpr uint16_t GEM_GEOMETRY_MAX_EDGES = 2048;
constexpr uint8_t GEM_GEOMETRY_MAX_EDGE_SUPPORTS = 8;

struct GemGeometryPlane
{
    double nx = 0.0;
    double ny = 0.0;
    double nz = 0.0;
    double distance = 0.0;
    float angleDegrees = 0.0f;
    float index = 0.0f;
    uint16_t tier = 0;
    uint16_t facet = 0;
    char name[GEM_SD_FACET_NAME_LENGTH] = {};
};

struct GemGeometryVertex
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct GemGeometryEdge
{
    uint16_t a = 0;
    uint16_t b = 0;
    uint8_t supportCount = 0;
    uint16_t supportPlanes[GEM_GEOMETRY_MAX_EDGE_SUPPORTS] = {};
};

struct GemRuntimeGeometry
{
    float radius = 1.0f;
    std::vector<GemGeometryPlane> planes;
    std::vector<GemGeometryVertex> vertices;
    std::vector<GemGeometryEdge> edges;

    void clear()
    {
        radius = 1.0f;
        planes.clear();
        vertices.clear();
        edges.clear();
    }
};

enum class GemGeometryResult : uint8_t
{
    OK,
    TOO_MANY_PLANES,
    NO_VERTICES,
    TOO_MANY_VERTICES,
    TOO_MANY_EDGES,
    BOUNDING_BOX_FAILED,
};

GemGeometryResult buildGemGeometry(const GemSdDesign& design,
                                   GemRuntimeGeometry* output);
const char* gemGeometryResultText(GemGeometryResult result);
