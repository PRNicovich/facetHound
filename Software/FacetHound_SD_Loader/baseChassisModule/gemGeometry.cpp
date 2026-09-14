#include "gemGeometry.h"

#include <algorithm>
#include <math.h>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kInsideTolerance = 1.0e-9;
constexpr double kDuplicateToleranceSquared = 2.5e-13; // 5e-7 model units
constexpr double kMinimumEdgeSquared = 1.0e-18;
constexpr double kMinimumFaceAreaSquared = 1.0e-24;
constexpr uint8_t kMaximumBoundingAttempts = 12;
constexpr size_t kMaxEdgeCandidates = GEM_GEOMETRY_MAX_EDGES * 2;
constexpr int16_t kArtificialFace = -1;

struct Vec3
{
    double x;
    double y;
    double z;
};

struct ClipFace
{
    int16_t plane = kArtificialFace;
    std::vector<Vec3> points;
};

struct ClipMesh
{
    std::vector<ClipFace> faces;
};

struct AngularPoint
{
    Vec3 point;
    double angle;
};

Vec3 add(const Vec3& a, const Vec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 subtract(const Vec3& a, const Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 scale(const Vec3& value, double amount)
{
    return {value.x * amount, value.y * amount, value.z * amount};
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

double dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

double lengthSquared(const Vec3& value)
{
    return dot(value, value);
}

Vec3 normalized(const Vec3& value)
{
    const double squared = lengthSquared(value);
    return squared > 0.0 ? scale(value, 1.0 / sqrt(squared)) : Vec3{0.0, 0.0, 0.0};
}

Vec3 planeNormal(const GemCutCoordinate& cut, const GemSdDesign& design)
{
    const double wheel = design.wheelIndex > 0.0 ? design.wheelIndex : 96.0;
    double indexOffset = fmod(cut.index - design.meridian, wheel);
    if (indexOffset < 0.0) indexOffset += wheel;
    const double azimuth = design.designIndexSign * 2.0 * kPi * indexOffset / wheel;
    const double tilt = cut.angleDegrees * kPi / 180.0;

    Vec3 normal = {
        sin(tilt) * cos(azimuth),
        sin(tilt) * sin(azimuth),
        cos(tilt),
    };
    if (cut.angleDegrees < 0.0)
        normal = scale(normal, -1.0);
    return normalized(normal);
}

double planeSide(const GemGeometryPlane& plane, const Vec3& point)
{
    return plane.nx * point.x + plane.ny * point.y +
           plane.nz * point.z - plane.distance;
}

bool samePoint(const Vec3& a, const Vec3& b)
{
    return lengthSquared(subtract(a, b)) <= kDuplicateToleranceSquared;
}

void appendDistinct(std::vector<Vec3>* points, const Vec3& point)
{
    if (points->empty() || !samePoint(points->back(), point))
        points->push_back(point);
}

void addUnique(std::vector<Vec3>* points, const Vec3& point)
{
    for (const Vec3& existing : *points)
        if (samePoint(existing, point)) return;
    points->push_back(point);
}

void cleanPolygon(std::vector<Vec3>* points)
{
    if (points->size() > 1 && samePoint(points->front(), points->back()))
        points->pop_back();
    if (points->size() < 3)
    {
        points->clear();
        return;
    }

    Vec3 area = {0.0, 0.0, 0.0};
    for (size_t i = 0; i < points->size(); ++i)
        area = add(area, cross((*points)[i], (*points)[(i + 1) % points->size()]));
    if (lengthSquared(area) <= kMinimumFaceAreaSquared) points->clear();
}

Vec3 segmentPlaneIntersection(const Vec3& a, const Vec3& b,
                              double sideA, double sideB,
                              const GemGeometryPlane& plane)
{
    // Classification is tolerant, but solve on the actual plane. If a point is
    // only "inside" because it is within tolerance, clamping chooses that near-
    // plane endpoint and the projection below removes its tiny residual.
    const double denominator = sideA - sideB;
    double t = fabs(denominator) > 1.0e-18 ? sideA / denominator : 0.5;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    Vec3 point = add(a, scale(subtract(b, a), t));
    const Vec3 normal = {plane.nx, plane.ny, plane.nz};
    return subtract(point, scale(normal, planeSide(plane, point)));
}

ClipFace clipFace(const ClipFace& input, const GemGeometryPlane& plane,
                  std::vector<Vec3>* capPoints)
{
    ClipFace output;
    output.plane = input.plane;
    if (input.points.empty()) return output;
    output.points.reserve(input.points.size() + 1);

    Vec3 previous = input.points.back();
    double previousSide = planeSide(plane, previous);
    bool previousInside = previousSide <= kInsideTolerance;
    for (const Vec3& current : input.points)
    {
        const double currentSide = planeSide(plane, current);
        const bool currentInside = currentSide <= kInsideTolerance;
        if (previousInside != currentInside)
        {
            const Vec3 intersection = segmentPlaneIntersection(
                previous, current, previousSide, currentSide, plane);
            appendDistinct(&output.points, intersection);
            addUnique(capPoints, intersection);
        }
        if (currentInside) appendDistinct(&output.points, current);
        previous = current;
        previousSide = currentSide;
        previousInside = currentInside;
    }
    cleanPolygon(&output.points);
    return output;
}

void orderCap(std::vector<Vec3>* points, const GemGeometryPlane& plane)
{
    if (points->size() < 3) return;
    Vec3 center = {0.0, 0.0, 0.0};
    for (const Vec3& point : *points) center = add(center, point);
    center = scale(center, 1.0 / points->size());

    const Vec3 normal = {plane.nx, plane.ny, plane.nz};
    const Vec3 reference = fabs(normal.z) < 0.8
                               ? Vec3{0.0, 0.0, 1.0}
                               : Vec3{0.0, 1.0, 0.0};
    const Vec3 axisU = normalized(cross(reference, normal));
    const Vec3 axisV = cross(normal, axisU);

    std::vector<AngularPoint> ordered;
    ordered.reserve(points->size());
    for (const Vec3& point : *points)
    {
        const Vec3 delta = subtract(point, center);
        ordered.push_back({point, atan2(dot(delta, axisV), dot(delta, axisU))});
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const AngularPoint& a, const AngularPoint& b)
              {
                  return a.angle < b.angle;
              });
    points->clear();
    points->reserve(ordered.size());
    for (const AngularPoint& item : ordered) appendDistinct(points, item.point);
    cleanPolygon(points);
}

ClipMesh makeBoundingCube(double radius)
{
    const Vec3 points[8] = {
        {-radius, -radius, -radius}, { radius, -radius, -radius},
        { radius,  radius, -radius}, {-radius,  radius, -radius},
        {-radius, -radius,  radius}, { radius, -radius,  radius},
        { radius,  radius,  radius}, {-radius,  radius,  radius},
    };
    const uint8_t faces[6][4] = {
        {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
        {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7},
    };

    ClipMesh mesh;
    mesh.faces.reserve(12);
    for (const auto& indices : faces)
    {
        ClipFace face;
        face.points.reserve(4);
        for (uint8_t index : indices) face.points.push_back(points[index]);
        mesh.faces.push_back(std::move(face));
    }
    return mesh;
}

bool clipMesh(ClipMesh* mesh, const GemGeometryPlane& plane, uint16_t planeId)
{
    bool anyInside = false;
    bool anyOutside = false;
    for (const ClipFace& face : mesh->faces)
    {
        for (const Vec3& point : face.points)
        {
            if (planeSide(plane, point) <= kInsideTolerance) anyInside = true;
            else anyOutside = true;
            if (anyInside && anyOutside) break;
        }
        if (anyInside && anyOutside) break;
    }
    if (!anyOutside) return true; // Redundant for the current convex mesh.
    if (!anyInside)
    {
        mesh->faces.clear();
        return false;
    }

    std::vector<ClipFace> clippedFaces;
    clippedFaces.reserve(mesh->faces.size() + 1);
    std::vector<Vec3> capPoints;
    capPoints.reserve(mesh->faces.size());
    for (const ClipFace& face : mesh->faces)
    {
        ClipFace clipped = clipFace(face, plane, &capPoints);
        if (!clipped.points.empty()) clippedFaces.push_back(std::move(clipped));
    }

    orderCap(&capPoints, plane);
    if (capPoints.size() >= 3)
    {
        ClipFace cap;
        cap.plane = int16_t(planeId);
        cap.points = std::move(capPoints);
        clippedFaces.push_back(std::move(cap));
    }
    mesh->faces = std::move(clippedFaces);
    return !mesh->faces.empty();
}

bool containsArtificialFace(const ClipMesh& mesh)
{
    for (const ClipFace& face : mesh.faces)
        if (face.plane == kArtificialFace) return true;
    return false;
}

int findVertex(const Vec3& point, const std::vector<GemGeometryVertex>& vertices)
{
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        const Vec3 existing = {vertices[i].x, vertices[i].y, vertices[i].z};
        if (samePoint(existing, point)) return int(i);
    }
    return -1;
}

bool edgeLess(const GemGeometryEdge& lhs, const GemGeometryEdge& rhs)
{
    return lhs.a < rhs.a || (lhs.a == rhs.a && lhs.b < rhs.b);
}

void addEdgeSupport(GemGeometryEdge* edge, uint16_t plane)
{
    for (uint8_t i = 0; i < edge->supportCount; ++i)
        if (edge->supportPlanes[i] == plane) return;
    if (edge->supportCount < GEM_GEOMETRY_MAX_EDGE_SUPPORTS)
        edge->supportPlanes[edge->supportCount++] = plane;
}

GemGeometryResult extractGeometry(const ClipMesh& mesh, GemRuntimeGeometry* output)
{
    std::vector<GemGeometryEdge> candidates;
    candidates.reserve(GEM_GEOMETRY_MAX_EDGES * 2);
    std::vector<uint16_t> faceVertices;

    for (const ClipFace& face : mesh.faces)
    {
        if (face.plane < 0 || face.points.size() < 3) continue;
        const uint16_t planeId = uint16_t(face.plane);
        faceVertices.clear();
        faceVertices.reserve(face.points.size());
        for (const Vec3& point : face.points)
        {
            int vertexId = findVertex(point, output->vertices);
            if (vertexId < 0)
            {
                if (output->vertices.size() >= GEM_GEOMETRY_MAX_VERTICES)
                    return GemGeometryResult::TOO_MANY_VERTICES;
                GemGeometryVertex vertex;
                vertex.x = point.x;
                vertex.y = point.y;
                vertex.z = point.z;
                output->vertices.push_back(vertex);
                vertexId = int(output->vertices.size()) - 1;
            }
            if (faceVertices.empty() || faceVertices.back() != uint16_t(vertexId))
                faceVertices.push_back(uint16_t(vertexId));
        }
        if (faceVertices.size() > 1 && faceVertices.front() == faceVertices.back())
            faceVertices.pop_back();
        if (faceVertices.size() < 3) continue;

        for (size_t i = 0; i < faceVertices.size(); ++i)
        {
            uint16_t a = faceVertices[i];
            uint16_t b = faceVertices[(i + 1) % faceVertices.size()];
            if (a == b) continue;
            const Vec3 pointA = {output->vertices[a].x, output->vertices[a].y,
                                 output->vertices[a].z};
            const Vec3 pointB = {output->vertices[b].x, output->vertices[b].y,
                                 output->vertices[b].z};
            if (lengthSquared(subtract(pointA, pointB)) <= kMinimumEdgeSquared)
                continue;
            if (candidates.size() >= kMaxEdgeCandidates)
                return GemGeometryResult::TOO_MANY_EDGES;
            GemGeometryEdge edge;
            edge.a = min(a, b);
            edge.b = max(a, b);
            addEdgeSupport(&edge, planeId);
            candidates.push_back(edge);
        }
    }

    std::sort(candidates.begin(), candidates.end(), edgeLess);
    for (const GemGeometryEdge& candidate : candidates)
    {
        if (!output->edges.empty() && output->edges.back().a == candidate.a &&
            output->edges.back().b == candidate.b)
        {
            for (uint8_t support = 0; support < candidate.supportCount; ++support)
                addEdgeSupport(&output->edges.back(), candidate.supportPlanes[support]);
            continue;
        }
        if (output->edges.size() >= GEM_GEOMETRY_MAX_EDGES)
            return GemGeometryResult::TOO_MANY_EDGES;
        output->edges.push_back(candidate);
    }
    return output->vertices.empty() || output->edges.empty()
               ? GemGeometryResult::NO_VERTICES
               : GemGeometryResult::OK;
}
} // namespace

GemGeometryResult buildGemGeometry(const GemSdDesign& design,
                                   GemRuntimeGeometry* output)
{
    if (!output) return GemGeometryResult::NO_VERTICES;
    output->clear();
    if (design.cuts.size() > GEM_GEOMETRY_MAX_PLANES)
        return GemGeometryResult::TOO_MANY_PLANES;

    output->planes.reserve(design.cuts.size());
    output->vertices.reserve(GEM_GEOMETRY_MAX_VERTICES);
    output->edges.reserve(GEM_GEOMETRY_MAX_EDGES);
    double maximumDistance = 0.0;
    for (const GemCutCoordinate& cut : design.cuts)
    {
        const Vec3 normal = planeNormal(cut, design);
        GemGeometryPlane plane;
        plane.nx = normal.x;
        plane.ny = normal.y;
        plane.nz = normal.z;
        plane.distance = cut.gemcadDistance;
        plane.angleDegrees = float(cut.angleDegrees);
        plane.index = float(cut.index);
        plane.tier = cut.tier;
        plane.facet = cut.facet;
        snprintf(plane.name, sizeof(plane.name), "%s", cut.name);
        output->planes.push_back(plane);
        maximumDistance = max(maximumDistance, fabs(plane.distance));
    }
    if (output->planes.size() < 4) return GemGeometryResult::NO_VERTICES;

    std::vector<uint16_t> planeOrder(output->planes.size());
    for (uint16_t i = 0; i < planeOrder.size(); ++i) planeOrder[i] = i;
    std::stable_sort(planeOrder.begin(), planeOrder.end(),
                     [&](uint16_t a, uint16_t b)
                     {
                         return output->planes[a].distance < output->planes[b].distance;
                     });

    double boundingRadius = max(1.0, maximumDistance * 2.0);
    ClipMesh boundedMesh;
    bool bounded = false;
    for (uint8_t attempt = 0; attempt < kMaximumBoundingAttempts; ++attempt)
    {
        ClipMesh candidate = makeBoundingCube(boundingRadius);
        bool nonempty = true;
        for (uint16_t planeId : planeOrder)
        {
            if (!clipMesh(&candidate, output->planes[planeId], planeId))
            {
                nonempty = false;
                break;
            }
            yield();
        }
        if (nonempty && !containsArtificialFace(candidate))
        {
            boundedMesh = std::move(candidate);
            bounded = true;
            break;
        }
        boundingRadius *= 4.0;
        if (!isfinite(boundingRadius)) break;
    }
    if (!bounded)
    {
        output->clear();
        return GemGeometryResult::BOUNDING_BOX_FAILED;
    }

    const GemGeometryResult extracted = extractGeometry(boundedMesh, output);
    if (extracted != GemGeometryResult::OK)
    {
        output->clear();
        return extracted;
    }

    double radiusSquared = 0.0;
    for (const GemGeometryVertex& vertex : output->vertices)
        radiusSquared = max(radiusSquared,
                            lengthSquared({vertex.x, vertex.y, vertex.z}));
    output->radius = radiusSquared > 1.0e-12 ? float(sqrt(radiusSquared)) : 1.0f;
    return GemGeometryResult::OK;
}

const char* gemGeometryResultText(GemGeometryResult result)
{
    switch (result)
    {
        case GemGeometryResult::OK:                  return "OK";
        case GemGeometryResult::TOO_MANY_PLANES:     return "GEOMETRY_TOO_MANY_PLANES";
        case GemGeometryResult::NO_VERTICES:         return "GEOMETRY_NO_VERTICES";
        case GemGeometryResult::TOO_MANY_VERTICES:   return "GEOMETRY_TOO_MANY_VERTICES";
        case GemGeometryResult::TOO_MANY_EDGES:      return "GEOMETRY_TOO_MANY_EDGES";
        case GemGeometryResult::BOUNDING_BOX_FAILED: return "GEOMETRY_UNBOUNDED";
        default:                                     return "GEOMETRY_ERROR";
    }
}
