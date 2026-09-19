#pragma once

#include <Arduino.h>
#include <vector>

constexpr uint16_t RUNTIME_MESH_MAX_PLANES = 512;
constexpr uint16_t RUNTIME_MESH_MAX_VERTICES = 1024;
constexpr uint16_t RUNTIME_MESH_MAX_EDGES = 2048;
constexpr uint8_t RUNTIME_MESH_MAX_EDGE_SUPPORTS = 8;
constexpr size_t RUNTIME_MESH_PLANE_NAME_LENGTH = 12;

struct RuntimeMeshVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct RuntimeMeshEdge
{
    uint16_t a = 0;
    uint16_t b = 0;
    uint16_t supportStart = 0;
    uint8_t supportCount = 0;
};

struct RuntimeMeshPlane
{
    float tipDegrees = 0.0f;
    float twistTicks = 0.0f;
    float nx = 0, ny = 0, nz = 1;
    bool normalValid = false;
    uint16_t tier = 0;
    uint16_t facet = 0;
    char name[RUNTIME_MESH_PLANE_NAME_LENGTH] = {};
};

class RuntimeGemMesh
{
public:
    bool beginTransfer(uint16_t vertices, uint16_t edges, uint16_t planes,
                       float indexResolution, float radius);
    bool setVertex(uint16_t id, float x, float y, float z);
    bool setEdge(uint16_t id, uint16_t a, uint16_t b, uint8_t supportCount,
                 const uint16_t* supportPlanes);
    bool setPlane(uint16_t id, float tipDegrees, float twistTicks,
                  uint16_t tier, uint16_t facet, const char* name);
    bool finishTransfer();
    void clear();

    bool active() const { return active_; }
    float indexResolution() const { return indexResolution_; }
    float radius() const { return radius_; }
    int indexSign() const { return indexSign_; }
    void setIndexSign(int sign) { indexSign_ = sign < 0 ? -1 : 1; }
    const std::vector<RuntimeMeshVertex>& vertices() const { return vertices_; }
    const std::vector<RuntimeMeshEdge>& edges() const { return edges_; }
    const std::vector<RuntimeMeshPlane>& planes() const { return planes_; }
    int findPlane(uint16_t tier, uint16_t facet) const;
    bool edgeHasPlane(uint16_t edge, uint16_t plane) const;
    size_t storageBytes() const;
    bool edgeVisibleFromCap(uint16_t edge, bool top) const;

private:
    bool active_ = false;
    bool receiving_ = false;
    float indexResolution_ = 96.0f;
    float radius_ = 1.0f;
    int indexSign_ = 1;
    uint16_t nextVertex_ = 0;
    uint16_t nextEdge_ = 0;
    uint16_t nextPlane_ = 0;
    std::vector<RuntimeMeshVertex> vertices_;
    std::vector<RuntimeMeshEdge> edges_;
    std::vector<RuntimeMeshPlane> planes_;
    std::vector<uint16_t> edgeSupports_;
};

RuntimeGemMesh& runtimeGemMesh();
bool selectCachedMesh(uint64_t id);
void commitCachedMesh();
