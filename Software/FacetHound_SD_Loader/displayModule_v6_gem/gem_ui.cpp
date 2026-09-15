#include "gem_ui.h"

#include <math.h>

#include "gem_data.h"

namespace
{
constexpr uint16_t C_BG = TFT_BLACK;
constexpr uint16_t C_TEXT = 0xE71C;
constexpr uint16_t C_DIM = 0x632C;
constexpr uint16_t C_NAME = 0x9CF3;
constexpr uint16_t C_EDGE = 0x94B2;
constexpr uint16_t C_PANEL = 0x10C3;
constexpr uint16_t C_GREEN = 0x27E8;
constexpr uint16_t C_YELLOW = 0xFEA0;
constexpr uint16_t C_CYAN = 0x3E7F;
constexpr uint16_t C_MAGENTA = 0xF95B;
constexpr uint16_t C_AMBER = 0xFCE0;

uint16_t gray565(float depth, float radius)
{
    const float normalized = constrain(0.5f + 0.5f * depth / max(radius, 1.0e-6f), 0.0f, 1.0f);
    const float contrast = normalized * normalized;
    const uint8_t level = uint8_t(28.0f + contrast * 227.0f);
    return uint16_t((level >> 3) << 11) | uint16_t((level >> 2) << 5) | uint16_t(level >> 3);
}

float machineTip(float storedTip)
{
    if (storedTip < 0.0f) return -storedTip;
    if (storedTip > 90.0f) return 180.0f - storedTip;
    return storedTip;
}

bool oppositeApproach(float storedTip)
{
    return storedTip < 0.0f || storedTip > 90.0f;
}

void drawDepthLine(TFT_eSprite& canvas, int x1, int y1, int x2, int y2,
                   uint16_t color, float depth, float radius, bool selected)
{
    const float normalized = constrain(0.5f + 0.5f * depth / max(radius, 1.0e-6f), 0.0f, 1.0f);
    const uint8_t width = selected ? 2 : normalized > 0.82f ? 3 : normalized > 0.55f ? 2 : 1;
    canvas.drawLine(x1, y1, x2, y2, color);
    if (width < 2) return;
    const bool mostlyHorizontal = abs(x2 - x1) >= abs(y2 - y1);
    canvas.drawLine(x1 + (mostlyHorizontal ? 0 : 1), y1 + (mostlyHorizontal ? 1 : 0),
                    x2 + (mostlyHorizontal ? 0 : 1), y2 + (mostlyHorizontal ? 1 : 0), color);
    if (width < 3) return;
    canvas.drawLine(x1 - (mostlyHorizontal ? 0 : 1), y1 - (mostlyHorizontal ? 1 : 0),
                    x2 - (mostlyHorizontal ? 0 : 1), y2 - (mostlyHorizontal ? 1 : 0), color);
}

void drawStepIndicator(TFT_eSprite& canvas, int x, int y, int selected, uint16_t color)
{
    selected = constrain(selected, 0, 2);
    const int activeBars = 3 - selected;
    for (int i = 0; i < 3; ++i)
    {
        const int height = 4 + i * 3;
        canvas.drawRect(x + i * 4, y + 10 - height, 3, height,
                        i < activeBars ? color : C_DIM);
        if (i < activeBars)
            canvas.fillRect(x + i * 4 + 1, y + 11 - height, 1, height - 2, color);
    }
}

void drawRotationArrow(TFT_eSprite& canvas, int x, int y, bool clockwise,
                       bool running, uint16_t color)
{
    const uint16_t arrowColor = running ? color : C_DIM;
    // A broken circle plus arrowhead remains legible at the HUD's small size.
    canvas.drawCircle(x, y, 7, arrowColor);
    canvas.fillCircle(x, y + (clockwise ? -7 : 7), 2, C_BG);
    if (clockwise)
        canvas.fillTriangle(x + 2, y - 10, x + 9, y - 7, x + 4, y - 3, arrowColor);
    else
        canvas.fillTriangle(x - 2, y + 10, x - 9, y + 7, x - 4, y + 3, arrowColor);
}

void drawWaterDrop(TFT_eSprite& canvas, int x, int y, uint16_t color)
{
    canvas.fillTriangle(x, y - 8, x - 5, y, x + 5, y, color);
    canvas.fillCircle(x, y + 2, 5, color);
    canvas.fillCircle(x - 2, y, 2, C_BG);
}

void drawDegreeGlyph(TFT_eSprite& canvas, int x, int y, uint16_t color)
{
    canvas.drawCircle(x, y, 3, color);
    canvas.drawCircle(x, y, 2, color);
}

void drawSmallAxisSymbol(TFT_eSprite& canvas, int x, int y, bool theta,
                         uint16_t color)
{
    canvas.drawCircle(x, y, 7, color);
    if (theta) canvas.drawFastHLine(x - 8, y, 17, color);
    else canvas.drawFastVLine(x, y - 10, 21, color);
}

void textAt(TFT_eSprite& canvas, const char* text, int x, int y, uint16_t color,
            uint8_t font = 2, uint8_t datum = TL_DATUM)
{
    canvas.setTextFont(font);
    canvas.setTextDatum(datum);
    canvas.setTextColor(color, C_BG);
    canvas.drawString(text, x, y);
}
}  // namespace

GemUi::GemUi(TFT_eSPI& display)
    : tft_(display), gemCanvas_(&display), hudCanvas_(&display)
{
}

bool GemUi::begin(DisplayMode mode)
{
    mode_ = mode;
    tft_.fillScreen(C_BG);

    gemCanvas_.setColorDepth(8);
    gemCanvasReady_ = gemCanvas_.createSprite(320, kGemHeight) != nullptr;

    hudCanvas_.setColorDepth(8);
    hudCanvasReady_ = hudCanvas_.createSprite(320, kHudHeight) != nullptr;

    poseInitialized_ = false;
    lastStateVersion_ = UINT32_MAX;
    lastStaticVersion_ = UINT32_MAX;
    lastStaticLinkAlive_ = false;
    lastFrameMs_ = 0;
    lastHudMs_ = 0;

    if (!gemCanvasReady_ || !hudCanvasReady_)
    {
        tft_.fillScreen(C_BG);
        tft_.setTextColor(TFT_RED, C_BG);
        tft_.setTextFont(2);
        tft_.drawString("Gem UI sprite allocation failed", 8, 8);
        return false;
    }

    gemCanvas_.fillSprite(C_BG);
    hudCanvas_.fillSprite(C_BG);
    return true;
}

void GemUi::invalidate()
{
    lastStateVersion_ = UINT32_MAX;
    lastStaticVersion_ = UINT32_MAX;
    lastStaticLinkAlive_ = !lastStaticLinkAlive_;
    lastFrameMs_ = 0;
    lastHudMs_ = 0;
}

float GemUi::wrappedDelta(float target, float current, float period) const
{
    if (period <= 0.0f) return target - current;
    float delta = fmodf(target - current, period);
    if (delta > period * 0.5f) delta -= period;
    if (delta < -period * 0.5f) delta += period;
    return delta;
}

float GemUi::normalizedTwist(float ticks, float wheelIndex) const
{
    if (!isfinite(ticks)) return 0.0f;
    const float meshResolution = runtimeGemMesh().active()
                                     ? runtimeGemMesh().indexResolution()
                                     : float(GemData::kIndexResolution);
    if (!isfinite(wheelIndex) || wheelIndex <= 0.0f)
        wheelIndex = meshResolution;

    float normalized = ticks * meshResolution / wheelIndex;
    normalized = fmodf(normalized, meshResolution);
    if (normalized < 0.0f) normalized += meshResolution;
    return normalized;
}

uint16_t GemUi::nearestPlane(float tipDegrees, float twistTicks) const
{
    uint16_t best = 0;
    float bestDistance = INFINITY;
    const RuntimeGemMesh& runtime = runtimeGemMesh();
    const float resolution = runtime.active() ? runtime.indexResolution()
                                               : float(GemData::kIndexResolution);
    const float degreesPerTick = 360.0f / resolution;
    const uint16_t count = runtime.active() ? uint16_t(runtime.planes().size())
                                             : GemData::kPlaneCount;

    for (uint16_t i = 0; i < count; ++i)
    {
        float planeTip = runtime.active() ? runtime.planes()[i].tipDegrees
                                           : GemData::kPlanes[i].tipDegrees;
        float planeTwist = runtime.active() ? runtime.planes()[i].twistTicks
                                             : GemData::kPlanes[i].twistTicks;
        if (oppositeApproach(planeTip)) planeTwist += resolution * 0.5f;
        planeTip = machineTip(planeTip);
        float dTip = planeTip - tipDegrees;
        float dTwist = wrappedDelta(
            planeTwist, twistTicks, resolution
        ) * degreesPerTick;
        float distance = dTip * dTip + dTwist * dTwist;

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

bool GemUi::edgeSelected(uint16_t edgeIndex, uint16_t plane) const
{
    const RuntimeGemMesh& runtime = runtimeGemMesh();
    if (runtime.active())
    {
        if (edgeIndex >= runtime.edges().size() || plane >= runtime.planes().size())
            return false;
        return runtime.edgeHasPlane(edgeIndex, plane);
    }

    if (edgeIndex >= GemData::kEdgeCount || plane >= GemData::kPlaneCount) return false;
    const auto& edge = GemData::kEdges[edgeIndex];
    return (edge.planeMask[plane >> 5] & (uint32_t(1) << (plane & 31))) != 0;
}

void GemUi::inferredTierName(uint16_t plane, char* text, size_t size) const
{
    const RuntimeGemMesh& runtime = runtimeGemMesh();
    const uint16_t count = runtime.active() ? uint16_t(runtime.planes().size())
                                             : GemData::kPlaneCount;
    if (plane >= count) { snprintf(text, size, "T?"); return; }
    auto angleAt = [&](uint16_t i) {
        return runtime.active() ? runtime.planes()[i].tipDegrees
                                : GemData::kPlanes[i].tipDegrees;
    };
    auto tierAt = [&](uint16_t i) {
        return runtime.active() ? runtime.planes()[i].tier : GemData::kPlanes[i].tier;
    };
    auto kindAt = [&](uint16_t i) {
        const float a = angleAt(i);
        if (runtime.active()) {
            if (fabsf(a) <= 0.05f) return 'T';
            if (fabsf(fabsf(a) - 90.0f) <= 0.05f) return 'G';
            return a < 0.0f ? 'P' : 'C';
        }
        if (fabsf(a - 90.0f) <= 0.05f) return 'G';
        if (a <= 0.05f || a >= 179.95f) return 'T';
        return a > 90.0f ? 'P' : 'C';
    };
    const char kind = kindAt(plane);
    if (kind == 'T' || kind == 'G') { snprintf(text, size, "%c", kind); return; }
    uint16_t ordinal = 1;
    const uint16_t tier = tierAt(plane);
    uint16_t previousTier = UINT16_MAX;
    for (uint16_t i = 0; i < plane && tierAt(i) < tier; ++i) {
        if (kindAt(i) == kind && tierAt(i) != previousTier) {
            ++ordinal;
            previousTier = tierAt(i);
        }
    }
    snprintf(text, size, "%c%u", kind, ordinal);
}

float GemUi::selectedTargetTip(const GemTelemetry& state) const
{
    if (state.jobActive && isfinite(state.jobAngle)) return machineTip(state.jobAngle);
    const RuntimeGemMesh& runtime = runtimeGemMesh();
    if (runtime.active() && selectedPlane_ < runtime.planes().size())
        return machineTip(runtime.planes()[selectedPlane_].tipDegrees);
    if (selectedPlane_ < GemData::kPlaneCount)
        return machineTip(GemData::kPlanes[selectedPlane_].tipDegrees);
    return state.tipDegrees;
}

float GemUi::selectedTargetTwist(const GemTelemetry& state) const
{
    const RuntimeGemMesh& runtime = runtimeGemMesh();
    const float resolution = runtime.active() ? runtime.indexResolution()
                                               : float(GemData::kIndexResolution);
    float twist = state.jobActive && isfinite(state.jobIndex) ? state.jobIndex
                                                               : state.targetTwist;
    float storedTip = state.jobAngle;
    if (!state.jobActive && runtime.active() && selectedPlane_ < runtime.planes().size()) {
        twist = runtime.planes()[selectedPlane_].twistTicks;
        storedTip = runtime.planes()[selectedPlane_].tipDegrees;
    } else if (!state.jobActive && selectedPlane_ < GemData::kPlaneCount) {
        twist = GemData::kPlanes[selectedPlane_].twistTicks;
        storedTip = GemData::kPlanes[selectedPlane_].tipDegrees;
    }
    if (oppositeApproach(storedTip)) twist += resolution * 0.5f;
    twist = fmodf(twist, resolution);
    if (twist < 0.0f) twist += resolution;
    return twist;
}

void GemUi::formatTierFacet(const GemTelemetry& state, char* text, size_t size,
                            bool compact) const
{
    const RuntimeGemMesh& runtime = runtimeGemMesh();
    const char* name = nullptr;
    uint16_t tier = 0;
    uint16_t facet = 0;
    float angle = 0.0f;

    if (state.jobActive)
    {
        name = state.jobFacetName;
        tier = state.jobTier;
        facet = state.jobFacet;
        angle = state.jobAngle;
    }
    else if (runtime.active() && selectedPlane_ < runtime.planes().size())
    {
        const RuntimeMeshPlane& plane = runtime.planes()[selectedPlane_];
        name = plane.name;
        tier = plane.tier;
        facet = plane.facet;
        angle = plane.tipDegrees;
    }
    else if (selectedPlane_ < GemData::kPlaneCount)
    {
        const auto& plane = GemData::kPlanes[selectedPlane_];
        tier = plane.tier;
        facet = plane.facet;
        angle = plane.tipDegrees;
    }

    char inferred[8];
    if (name && name[0]) snprintf(inferred, sizeof(inferred), "%s", name);
    else inferredTierName(selectedPlane_, inferred, sizeof(inferred));

    uint16_t facetCount = 0;
    if (runtime.active()) {
        for (const RuntimeMeshPlane& plane : runtime.planes())
            if (plane.tier == tier) ++facetCount;
    } else {
        for (uint16_t i = 0; i < GemData::kPlaneCount; ++i)
            if (GemData::kPlanes[i].tier == tier) ++facetCount;
    }
    facetCount = max(facetCount, facet);
    if (compact) snprintf(text, size, "%s - %u/%u", inferred, facet, facetCount);
    else snprintf(text, size, "%s   %u/%u", inferred, facet, facetCount);
}

void GemUi::updatePose(const GemTelemetry& state)
{
    const float meshResolution = runtimeGemMesh().active()
                                     ? runtimeGemMesh().indexResolution()
                                     : float(GemData::kIndexResolution);
    float targetTip = isfinite(state.tipDegrees) ? state.tipDegrees : 0.0f;
    float targetTwist = normalizedTwist(
        state.targetTwist + state.twistError, state.wheelIndex
    );

    if (!poseInitialized_)
    {
        displayedTip_ = targetTip;
        displayedTwist_ = targetTwist;
        poseInitialized_ = true;
    }
    else
    {
        displayedTip_ += (targetTip - displayedTip_) * 0.20f;
        displayedTwist_ += wrappedDelta(
            targetTwist, displayedTwist_, meshResolution
        ) * 0.20f;
        displayedTwist_ = normalizedTwist(displayedTwist_, meshResolution);
    }

    int jobPlane = state.jobActive
                       ? runtimeGemMesh().findPlane(state.jobTier, state.jobFacet)
                       : -1;
    selectedPlane_ = jobPlane >= 0
                         ? uint16_t(jobPlane)
                         : nearestPlane(targetTip, normalizedTwist(
                                                       state.targetTwist,
                                                       state.wheelIndex));
}

void GemUi::drawHeader(const GemTelemetry& state, bool showTier)
{
    const char* title = state.jobActive && state.jobTitle && state.jobTitle[0]
                            ? state.jobTitle : GemData::kTitle;
    textAt(gemCanvas_, title, 7, 5, C_NAME, 1, TL_DATUM);

    if (showTier)
    {
        char facet[40];
        formatTierFacet(state, facet, sizeof(facet));
        textAt(gemCanvas_, facet, 313, 7, C_AMBER, 2, TR_DATUM);
    }
}

void GemUi::drawDynamic(const GemTelemetry& state)
{
    gemCanvas_.fillSprite(C_BG);
    drawHeader(state, true);

    const RuntimeGemMesh& runtime = runtimeGemMesh();
    const float resolution = runtime.active() ? runtime.indexResolution()
                                               : float(GemData::kIndexResolution);
    float storedSelectedTip = 0.0f;
    if (runtime.active() && selectedPlane_ < runtime.planes().size())
        storedSelectedTip = runtime.planes()[selectedPlane_].tipDegrees;
    else if (selectedPlane_ < GemData::kPlaneCount)
        storedSelectedTip = GemData::kPlanes[selectedPlane_].tipDegrees;
    // Crown/table approach the lap from the opposite X rotation; pavilion uses
    // the acute angle after the half-wheel approach correction.
    const float tip = displayedTip_ * DEG_TO_RAD *
                      (oppositeApproach(storedSelectedTip) ? 1.0f : -1.0f);
    const bool reverseView = oppositeApproach(storedSelectedTip) ||
                             fabsf(fabsf(storedSelectedTip) - 90.0f) <= 0.05f;
    const float twist = displayedTwist_ * TWO_PI / resolution;
    const float ct = cosf(tip), st = sinf(tip);
    const float cz = cosf(twist), sz = sinf(twist);
    const float scale = 116.0f / (runtime.active() ? runtime.radius() : GemData::kRadius);
    constexpr float centerX = 160.0f;
    constexpr float centerY = 166.0f;

    const uint16_t vertexCount = runtime.active() ? uint16_t(runtime.vertices().size())
                                                   : GemData::kVertexCount;
    for (uint16_t i = 0; i < vertexCount; ++i)
    {
        const float px = runtime.active() ? runtime.vertices()[i].x : GemData::kVertices[i].x;
        const float py = runtime.active() ? runtime.vertices()[i].y : GemData::kVertices[i].y;
        const float pz = runtime.active() ? runtime.vertices()[i].z : GemData::kVertices[i].z;
        const float x1 = cz * px - sz * py;
        const float y1 = sz * px + cz * py;
        const float y2 = ct * y1 - st * pz;
        const float z2 = st * y1 + ct * pz;
        const float viewX = reverseView ? -x1 : x1;
        const float viewDepth = reverseView ? -z2 : z2;
        screenX_[i] = int16_t(lroundf(centerX + scale * viewX));
        screenY_[i] = int16_t(lroundf(centerY - scale * y2));
        screenDepth_[i] = viewDepth;
    }

    // Base wireframe first, selected-facet edges last so the highlight stays crisp.
    for (uint8_t pass = 0; pass < 2; ++pass)
    {
        const uint16_t edgeCount = runtime.active() ? uint16_t(runtime.edges().size())
                                                     : GemData::kEdgeCount;
        for (uint16_t i = 0; i < edgeCount; ++i)
        {
            bool selected = edgeSelected(i, selectedPlane_);
            if (selected != (pass == 1)) continue;
            uint16_t a = runtime.active() ? runtime.edges()[i].a : GemData::kEdges[i].a;
            uint16_t b = runtime.active() ? runtime.edges()[i].b : GemData::kEdges[i].b;
            const uint16_t color = selected
                                       ? C_GREEN
                                       : gray565(0.5f * (screenDepth_[a] + screenDepth_[b]),
                                                 runtime.active() ? runtime.radius() : GemData::kRadius);
            drawDepthLine(gemCanvas_, screenX_[a], screenY_[a], screenX_[b],
                          screenY_[b], color,
                          0.5f * (screenDepth_[a] + screenDepth_[b]),
                          runtime.active() ? runtime.radius() : GemData::kRadius,
                          selected);
        }
    }

    gemCanvas_.drawFastHLine(14, 316, 292, C_PANEL);
}

void GemUi::drawStatic(const GemTelemetry& state)
{
    gemCanvas_.fillSprite(C_BG);
    drawHeader(state, true);
    const RuntimeGemMesh& runtime = runtimeGemMesh();

    static const char* labels[] = {"T", "B", "F", "S"};
    static const int16_t boxes[][4] = {
        {18, 36, 138, 130}, {165, 36, 138, 130},
        {18, 180, 138, 130}, {165, 180, 138, 130},
    };

    for (uint8_t panel = 0; panel < 4; ++panel)
    {
        gemCanvas_.drawRect(
            boxes[panel][0], boxes[panel][1], boxes[panel][2], boxes[panel][3], C_PANEL
        );

        if (runtime.active())
        {
            float minU = INFINITY, maxU = -INFINITY;
            float minV = INFINITY, maxV = -INFINITY;
            for (uint16_t i = 0; i < runtime.vertices().size(); ++i)
            {
                const RuntimeMeshVertex& point = runtime.vertices()[i];
                float u = panel < 2 ? point.x : (panel == 2 ? point.x : point.y);
                float v = panel < 2 ? point.y : point.z;
                if (panel == 1) v = -v;
                minU = min(minU, u); maxU = max(maxU, u);
                minV = min(minV, v); maxV = max(maxV, v);
            }
            float centerU = 0.5f * (minU + maxU);
            float centerV = 0.5f * (minV + maxV);
            float spanU = max(maxU - minU, 1.0e-6f);
            float spanV = max(maxV - minV, 1.0e-6f);
            float scale = 0.88f * min(float(boxes[panel][2]) / spanU,
                                      float(boxes[panel][3]) / spanV);
            float centerX = boxes[panel][0] + 0.5f * boxes[panel][2];
            float centerY = boxes[panel][1] + 0.5f * boxes[panel][3];

            for (uint16_t i = 0; i < runtime.vertices().size(); ++i)
            {
                const RuntimeMeshVertex& point = runtime.vertices()[i];
                float u = panel < 2 ? point.x : (panel == 2 ? point.x : point.y);
                float v = panel < 2 ? point.y : point.z;
                if (panel == 1) v = -v;
                screenX_[i] = int16_t(lroundf(centerX + (u - centerU) * scale));
                screenY_[i] = int16_t(lroundf(centerY - (v - centerV) * scale));
            }

            float zMin = INFINITY, zMax = -INFINITY;
            float viewSpan = 0.0f;
            for (const RuntimeMeshVertex& point : runtime.vertices())
            {
                zMin = min(zMin, point.z); zMax = max(zMax, point.z);
                float viewValue = panel == 2 ? point.y : point.x;
                viewSpan = max(viewSpan, fabsf(viewValue));
            }
            float zTolerance = max(1.0e-7f, 0.025f * (zMax - zMin));

            for (uint8_t pass = 0; pass < 2; ++pass)
            {
                for (uint16_t i = 0; i < runtime.edges().size(); ++i)
                {
                    const RuntimeMeshEdge& edge = runtime.edges()[i];
                    const RuntimeMeshVertex& a = runtime.vertices()[edge.a];
                    const RuntimeMeshVertex& b = runtime.vertices()[edge.b];
                    bool visible = true;
                    if (panel < 2)
                    {
                        float zMid = 0.5f * (a.z + b.z);
                        bool girdle = fabsf(zMid) <= zTolerance ||
                                      (a.z <= 0.0f && b.z >= 0.0f) ||
                                      (b.z <= 0.0f && a.z >= 0.0f);
                        visible = girdle || (panel == 0 ? zMid > 0.0f : zMid < 0.0f);
                    }
                    else
                    {
                        // Front/side are complete projections; color below
                        // distinguishes edges on the rear half of the stone.
                        visible = true;
                    }
                    if (!visible) continue;
                    bool selected = edgeSelected(i, selectedPlane_);
                    if (selected != (pass == 1)) continue;
                    float depth = 0.0f;
                    if (panel == 0) depth = 0.5f * (a.z + b.z);
                    else if (panel == 1) depth = -0.5f * (a.z + b.z);
                    else if (panel == 2) depth = -0.5f * (a.y + b.y);
                    else depth = 0.5f * (a.x + b.x);
                    const bool behind = panel >= 2 && depth < 0.0f;
                    const uint16_t color = selected ? (behind ? 0x1384 : C_GREEN)
                                                    : (behind ? C_DIM : C_EDGE);
                    gemCanvas_.drawLine(screenX_[edge.a], screenY_[edge.a],
                                        screenX_[edge.b], screenY_[edge.b], color);
                }
            }
        }
        else
        {
            for (uint8_t pass = 0; pass < 2; ++pass)
            {
                for (uint16_t i = 0; i < GemData::kEdgeCount; ++i)
                {
                    const auto& edge = GemData::kEdges[i];
                    if (panel < 2 && (edge.viewMask & (1u << panel)) == 0) continue;
                    bool selected = edgeSelected(i, selectedPlane_);
                    if (selected != (pass == 1)) continue;
                    const auto& a = GemData::kStaticPoints[panel][edge.a];
                    const auto& b = GemData::kStaticPoints[panel][edge.b];
                    const auto& a3 = GemData::kVertices[edge.a];
                    const auto& b3 = GemData::kVertices[edge.b];
                    float depth = 0.0f;
                    if (panel == 0) depth = 0.5f * (a3.z + b3.z);
                    else if (panel == 1) depth = -0.5f * (a3.z + b3.z);
                    else if (panel == 2) depth = -0.5f * (a3.y + b3.y);
                    else depth = 0.5f * (a3.x + b3.x);
                    const bool behind = panel >= 2 && depth < 0.0f;
                    const uint16_t color = selected ? (behind ? 0x1384 : C_GREEN)
                                                    : (behind ? C_DIM : C_EDGE);
                    gemCanvas_.drawLine(a.x, a.y - 10, b.x, b.y - 10, color);
                }
            }
        }

        const uint8_t datum = panel == 0 ? TL_DATUM : panel == 1 ? TR_DATUM
                                  : panel == 2 ? BL_DATUM : BR_DATUM;
        const int labelX = (panel == 0 || panel == 2)
                               ? boxes[panel][0] + 5
                               : boxes[panel][0] + boxes[panel][2] - 5;
        const int labelY = panel < 2
                               ? boxes[panel][1] + 4
                               : boxes[panel][1] + boxes[panel][3] - 4;
        textAt(gemCanvas_, labels[panel], labelX, labelY, C_DIM, 2, datum);
    }

}

void GemUi::drawHud(const GemTelemetry& state)
{
    hudCanvas_.fillSprite(C_BG);
    hudCanvas_.drawFastHLine(8, 0, 304, C_PANEL);

    char line[64];
    const float targetTip = selectedTargetTip(state);
    const float tipError = state.tipDegrees - targetTip;
    snprintf(line, sizeof(line), "%+7.2f", targetTip);
    textAt(hudCanvas_, line, 200, 24, C_YELLOW, 6, MR_DATUM);
    drawDegreeGlyph(hudCanvas_, 208, 9, C_YELLOW);
    snprintf(line, sizeof(line), "%+.2f", tipError);
    textAt(hudCanvas_, line, 282, 24, C_YELLOW, 4, MR_DATUM);
    drawDegreeGlyph(hudCanvas_, 290, 14, C_YELLOW);

    const float resolution = runtimeGemMesh().active()
                                 ? runtimeGemMesh().indexResolution()
                                 : float(GemData::kIndexResolution);
    const float targetTwist = selectedTargetTwist(state);
    const float actualTwist = normalizedTwist(state.targetTwist + state.twistError,
                                               state.wheelIndex);
    const float indexError = wrappedDelta(actualTwist, targetTwist, resolution);
    snprintf(line, sizeof(line), "%+7.2f", targetTwist);
    textAt(hudCanvas_, line, 200, 72, C_CYAN, 6, MR_DATUM);
    snprintf(line, sizeof(line), "%+.2f", indexError);
    textAt(hudCanvas_, line, 282, 72, C_CYAN, 4, MR_DATUM);
    drawStepIndicator(hudCanvas_, 205, 49, state.indexStep, C_CYAN);

    hudCanvas_.drawFastHLine(8, 102, 304, C_PANEL);

    textAt(hudCanvas_, "Z", 13, 122, C_MAGENTA, 2, MC_DATUM);
    snprintf(line, sizeof(line), "%+8.3f", state.zMillimeters);
    textAt(hudCanvas_, line, 200, 122, C_MAGENTA, 6, MR_DATUM);
    drawStepIndicator(hudCanvas_, 205, 99, state.zStep, C_MAGENTA);
    textAt(hudCanvas_, "mm", 208, 144, C_MAGENTA, 2, BL_DATUM);

    const bool clockwise = state.rpmDirection == 1 || state.rpmDirection == 2;
    const bool motorRunning = state.rpmDirection == 0 || state.rpmDirection == 2;
    drawRotationArrow(hudCanvas_, 218, 108, clockwise, motorRunning, C_TEXT);
    snprintf(line, sizeof(line), "%lu rpm", static_cast<unsigned long>(state.rpmActual));
    textAt(hudCanvas_, line, 316, 108, C_TEXT, 2, MR_DATUM);

    drawWaterDrop(hudCanvas_, 218, 137,
                  fabsf(state.flow) > 0.01f ? C_CYAN : C_DIM);
    snprintf(line, sizeof(line), "%.1f mL/min", state.flow);
    textAt(hudCanvas_, line, 316, 137, C_TEXT, 2, MR_DATUM);

    drawSmallAxisSymbol(hudCanvas_, 14, 24, true, C_YELLOW);
    drawSmallAxisSymbol(hudCanvas_, 14, 72, false, C_CYAN);
}

void GemUi::pushGemCanvas()
{
    if (gemCanvasReady_) gemCanvas_.pushSprite(0, 0);
}

void GemUi::pushHudCanvas()
{
    if (hudCanvasReady_) hudCanvas_.pushSprite(0, kGemHeight);
}

void GemUi::tick(const GemTelemetry& state)
{
    const uint32_t now = millis();
    const bool stateChanged = state.version != lastStateVersion_;

    if (mode_ == DisplayMode::DYNAMIC)
    {
        if (now - lastFrameMs_ >= kFramePeriodMs)
        {
            lastFrameMs_ = now;
            updatePose(state);
            drawDynamic(state);
            pushGemCanvas();
        }
    }
    else if (mode_ == DisplayMode::STATIC)
    {
        const bool firstFrame = lastStaticVersion_ == UINT32_MAX;
        const bool linkChanged = state.linkAlive != lastStaticLinkAlive_;
        const uint16_t previousPlane = selectedPlane_;

        if (stateChanged || firstFrame) updatePose(state);

        if (firstFrame || linkChanged || selectedPlane_ != previousPlane)
        {
            drawStatic(state);
            pushGemCanvas();
        }

        lastStaticVersion_ = state.version;
        lastStaticLinkAlive_ = state.linkAlive;
    }

    if ((stateChanged && now - lastHudMs_ >= kHudPeriodMs) ||
        now - lastHudMs_ >= 500)
    {
        lastHudMs_ = now;
        drawHud(state);
        pushHudCanvas();
    }

    lastStateVersion_ = state.version;
}
