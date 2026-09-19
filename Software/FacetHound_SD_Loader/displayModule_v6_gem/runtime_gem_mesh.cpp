#include "runtime_gem_mesh.h"

#include <math.h>

namespace
{
RuntimeGemMesh mesh;
}

RuntimeGemMesh& runtimeGemMesh() { return mesh; }

void RuntimeGemMesh::clear()
{
    active_ = false;
    receiving_ = false;
    vertices_.clear();
    edges_.clear();
    planes_.clear();
    edgeSupports_.clear();
    nextVertex_ = nextEdge_ = nextPlane_ = 0;
}

bool RuntimeGemMesh::beginTransfer(uint16_t vertices, uint16_t edges,
                                   uint16_t planes, float indexResolution,
                                   float radius)
{
    clear();
    if (!vertices || !edges || !planes ||
        vertices > RUNTIME_MESH_MAX_VERTICES ||
        edges > RUNTIME_MESH_MAX_EDGES || planes > RUNTIME_MESH_MAX_PLANES ||
        !isfinite(indexResolution) || indexResolution <= 0.0f ||
        !isfinite(radius) || radius <= 0.0f)
        return false;

    vertices_.resize(vertices);
    edges_.resize(edges);
    planes_.resize(planes);
    edgeSupports_.reserve(size_t(edges) * 2);
    indexResolution_ = indexResolution;
    radius_ = radius;
    receiving_ = true;
    return true;
}

bool RuntimeGemMesh::setVertex(uint16_t id, float x, float y, float z)
{
    if (!receiving_ || id != nextVertex_ || id >= vertices_.size() ||
        !isfinite(x) || !isfinite(y) || !isfinite(z))
        return false;
    vertices_[id] = {x, y, z};
    ++nextVertex_;
    return true;
}

bool RuntimeGemMesh::setEdge(uint16_t id, uint16_t a, uint16_t b,
                             uint8_t supportCount, const uint16_t* supportPlanes)
{
    if (!receiving_ || !supportPlanes || id != nextEdge_ || id >= edges_.size() ||
        a >= vertices_.size() || b >= vertices_.size() || a == b ||
        supportCount < 2 || supportCount > RUNTIME_MESH_MAX_EDGE_SUPPORTS)
        return false;
    RuntimeMeshEdge& edge = edges_[id];
    edge.a = a;
    edge.b = b;
    edge.supportStart = uint16_t(edgeSupports_.size());
    edge.supportCount = supportCount;
    for (uint8_t support = 0; support < supportCount; ++support)
    {
        if (supportPlanes[support] >= planes_.size()) return false;
        edgeSupports_.push_back(supportPlanes[support]);
    }
    ++nextEdge_;
    return true;
}

bool RuntimeGemMesh::setPlane(uint16_t id, float tipDegrees, float twistTicks,
                              uint16_t tier, uint16_t facet, const char* name)
{
    if (!receiving_ || id != nextPlane_ || id >= planes_.size() ||
        !isfinite(tipDegrees) || !isfinite(twistTicks) || !tier || !facet)
        return false;
    RuntimeMeshPlane& plane = planes_[id];
    plane.tipDegrees = tipDegrees;
    plane.twistTicks = twistTicks;
    plane.tier = tier;
    plane.facet = facet;
    snprintf(plane.name, sizeof(plane.name), "%s", name ? name : "");
    ++nextPlane_;
    return true;
}

bool RuntimeGemMesh::finishTransfer()
{
    if (!receiving_ || nextVertex_ != vertices_.size() ||
        nextEdge_ != edges_.size() || nextPlane_ != planes_.size())
    {
        clear();
        return false;
    }
    receiving_ = false;
    active_ = true;
    // Derive outward face normals from the transferred geometry itself.
    // This avoids assuming the compiled example and imported ASC share axes.
    for (uint16_t p = 0; p < planes_.size(); ++p) {
        RuntimeMeshVertex anchor; bool haveAnchor = false;
        float best = 0, nx = 0, ny = 0, nz = 0;
        for (uint16_t e = 0; e < edges_.size(); ++e) {
            if (!edgeHasPlane(e, p)) continue;
            const auto& a = vertices_[edges_[e].a];
            const auto& b = vertices_[edges_[e].b];
            if (!haveAnchor) { anchor = a; haveAnchor = true; }
            float ax = a.x-anchor.x, ay = a.y-anchor.y, az = a.z-anchor.z;
            float bx = b.x-anchor.x, by = b.y-anchor.y, bz = b.z-anchor.z;
            float x = ay*bz-az*by, y = az*bx-ax*bz, z = ax*by-ay*bx;
            float length2 = x*x+y*y+z*z;
            if (length2 > best) { best = length2; nx=x; ny=y; nz=z; }
        }
        if (best > 1e-18f) {
            float factor = 1.0f/sqrtf(best);
            if (nx*anchor.x+ny*anchor.y+nz*anchor.z < 0) factor = -factor;
            planes_[p].nx=nx*factor; planes_[p].ny=ny*factor; planes_[p].nz=nz*factor;
            planes_[p].normalValid = true;
        }
    }
    return true;
}

int RuntimeGemMesh::findPlane(uint16_t tier, uint16_t facet) const
{
    if (!active_) return -1;
    for (size_t i = 0; i < planes_.size(); ++i)
        if (planes_[i].tier == tier && planes_[i].facet == facet) return int(i);
    return -1;
}

bool RuntimeGemMesh::edgeHasPlane(uint16_t edgeId, uint16_t plane) const
{
    if (!active_ || edgeId >= edges_.size()) return false;
    const RuntimeMeshEdge& edge = edges_[edgeId];
    for (uint8_t support = 0; support < edge.supportCount; ++support)
        if (edgeSupports_[edge.supportStart + support] == plane) return true;
    return false;
}
