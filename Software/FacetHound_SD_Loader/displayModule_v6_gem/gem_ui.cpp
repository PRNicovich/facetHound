#include "gem_ui.h"

#include <math.h>

#include "gem_data.h"

namespace
{
constexpr uint16_t C_BG = TFT_BLACK;
constexpr uint16_t C_TEXT = 0xE71C;
constexpr uint16_t C_DIM = 0x632C;
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
    const uint8_t level = uint8_t(70.0f + normalized * 155.0f);
    return uint16_t((level >> 3) << 11) | uint16_t((level >> 2) << 5) | uint16_t(level >> 3);
}

void drawAxisSymbol(TFT_eSprite& canvas, int x, int y, bool theta, uint16_t color)
{
    canvas.drawCircle(x, y, 10, color);
    if (theta)
        canvas.drawFastHLine(x - 9, y, 19, color);
    else
        canvas.drawFastVLine(x, y - 13, 27, color);
}

void drawStepIndicator(TFT_eSprite& canvas, int x, int y, int selected, uint16_t color)
{
    selected = constrain(selected, 0, 2);
    for (int i = 0; i < 3; ++i)
    {
        const int height = 5 + i * 4;
        canvas.drawRect(x + i * 7, y + 13 - height, 5, height, i == selected ? color : C_DIM);
        if (i == selected) canvas.fillRect(x + i * 7 + 1, y + 14 - height, 3, height - 2, color);
    }
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

float GemUi::selectedPlaneHeight(uint16_t plane) const
{
    float sum = 0.0f;
    uint32_t count = 0;
    const RuntimeGemMesh& runtime = runtimeGemMesh();
    if (runtime.active())
    {
        for (uint16_t i = 0; i < runtime.edges().size(); ++i)
        {
            if (!edgeSelected(i, plane)) continue;
            const RuntimeMeshEdge& edge = runtime.edges()[i];
            sum += runtime.vertices()[edge.a].z + runtime.vertices()[edge.b].z;
            count += 2;
        }
    }
    else
    {
        for (uint16_t i = 0; i < GemData::kEdgeCount; ++i)
        {
            if (!edgeSelected(i, plane)) continue;
            const auto& edge = GemData::kEdges[i];
            sum += GemData::kVertices[edge.a].z + GemData::kVertices[edge.b].z;
            count += 2;
        }
    }
    return count ? sum / float(count) : NAN;
}

void GemUi::formatTierFacet(const GemTelemetry& state, char* text, size_t size) const
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

    if (name && name[0])
    {
        snprintf(text, size, "%s   FACET %u", name, facet);
        return;
    }

    const float height = selectedPlaneHeight(selectedPlane_);
    const float radius = runtime.active() ? runtime.radius() : GemData::kRadius;
    const char* inferred = nullptr;
    if (fabsf(angle) >= 89.995f) inferred = "TABLE";
    else if (isfinite(height) && fabsf(height) <= radius * 0.07f) inferred = "GIRDLE";
    else if (isfinite(height)) inferred = height > 0.0f ? "CROWN" : "PAVILION";

    if (inferred) snprintf(text, size, "%s   FACET %u", inferred, facet);
    else snprintf(text, size, "TIER %u   FACET %u", tier, facet);
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
        displayedTip_ += (targetTip - displayedTip_) * 0.28f;
        displayedTwist_ += wrappedDelta(
            targetTwist, displayedTwist_, meshResolution
        ) * 0.28f;
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

void GemUi::drawHeader(const GemTelemetry& state)
{
    const char* title = state.jobActive && state.jobTitle && state.jobTitle[0]
                            ? state.jobTitle : GemData::kTitle;
    textAt(gemCanvas_, title, 7, 6, C_TEXT, 2, TL_DATUM);

    char facet[40];
    formatTierFacet(state, facet, sizeof(facet));
    textAt(gemCanvas_, facet, 7, 26, C_AMBER, 2, TL_DATUM);
}

void GemUi::drawDynamic(const GemTelemetry& state)
{
    gemCanvas_.fillSprite(C_BG);
    drawHeader(state);

    const RuntimeGemMesh& runtime = runtimeGemMesh();
    const float resolution = runtime.active() ? runtime.indexResolution()
                                               : float(GemData::kIndexResolution);
    const float tip = displayedTip_ * DEG_TO_RAD;
    const float twist = displayedTwist_ * TWO_PI / resolution;
    const float ct = cosf(tip), st = sinf(tip);
    const float cz = cosf(twist), sz = sinf(twist);
    const float scale = 116.0f / (runtime.active() ? runtime.radius() : GemData::kRadius);
    constexpr float centerX = 160.0f;
    constexpr float centerY = 178.0f;

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
        screenX_[i] = int16_t(lroundf(centerX + scale * x1));
        screenY_[i] = int16_t(lroundf(centerY - scale * y2));
        screenDepth_[i] = z2;
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
            gemCanvas_.drawLine(
                screenX_[a], screenY_[a], screenX_[b], screenY_[b],
                color
            );
        }
    }

    gemCanvas_.drawFastHLine(14, 316, 292, C_PANEL);
}

void GemUi::drawStatic(const GemTelemetry& state)
{
    gemCanvas_.fillSprite(C_BG);
    drawHeader(state);
    const RuntimeGemMesh& runtime = runtimeGemMesh();

    static const char* labels[] = {"TOP", "BOTTOM", "FRONT", "SIDE"};
    static const int16_t boxes[][4] = {
        {18, 46, 138, 130}, {165, 46, 138, 130},
        {18, 190, 138, 130}, {165, 190, 138, 130},
    };

    for (uint8_t panel = 0; panel < 4; ++panel)
    {
        gemCanvas_.drawRect(
            boxes[panel][0], boxes[panel][1], boxes[panel][2], boxes[panel][3], C_PANEL
        );
        textAt(gemCanvas_, labels[panel], boxes[panel][0] + 4,
               boxes[panel][1] + 3, C_DIM, 1, TL_DATUM);

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
                        float midpoint = panel == 2 ? 0.5f * (a.y + b.y)
                                                    : 0.5f * (a.x + b.x);
                        float sign = panel == 2 ? -1.0f : 1.0f;
                        visible = sign * midpoint >= -0.04f * (viewSpan + 1.0e-9f);
                    }
                    if (!visible) continue;
                    bool selected = edgeSelected(i, selectedPlane_);
                    if (selected != (pass == 1)) continue;
                    gemCanvas_.drawLine(screenX_[edge.a], screenY_[edge.a],
                                        screenX_[edge.b], screenY_[edge.b],
                                        selected ? C_GREEN : C_EDGE);
                }
            }
            continue;
        }

        for (uint8_t pass = 0; pass < 2; ++pass)
        {
            for (uint16_t i = 0; i < GemData::kEdgeCount; ++i)
            {
                const auto& edge = GemData::kEdges[i];
                if ((edge.viewMask & (1u << panel)) == 0) continue;
                bool selected = edgeSelected(i, selectedPlane_);
                if (selected != (pass == 1)) continue;
                const auto& a = GemData::kStaticPoints[panel][edge.a];
                const auto& b = GemData::kStaticPoints[panel][edge.b];
                gemCanvas_.drawLine(a.x, a.y, b.x, b.y, selected ? C_GREEN : C_EDGE);
            }
        }
    }
}

void GemUi::drawHud(const GemTelemetry& state)
{
    hudCanvas_.fillSprite(C_BG);
    hudCanvas_.drawFastHLine(8, 0, 304, C_PANEL);

    char line[64];
    drawAxisSymbol(hudCanvas_, 20, 28, true, C_YELLOW);
    textAt(hudCanvas_, "TIP", 38, 7, C_YELLOW, 1, TL_DATUM);
    snprintf(line, sizeof(line), "%+.2f", state.tipDegrees);
    textAt(hudCanvas_, line, 307, 28, C_YELLOW, 6, MR_DATUM);

    float actualTwist = state.targetTwist + state.twistError;
    drawAxisSymbol(hudCanvas_, 20, 76, false, C_CYAN);
    textAt(hudCanvas_, "INDEX", 38, 55, C_CYAN, 1, TL_DATUM);
    snprintf(line, sizeof(line), "%+.2f", actualTwist);
    textAt(hudCanvas_, line, 252, 76, C_CYAN, 6, MR_DATUM);
    snprintf(line, sizeof(line), "ERR %+.2f", state.twistError);
    textAt(hudCanvas_, line, 312, 88, C_DIM, 1, TR_DATUM);
    drawStepIndicator(hudCanvas_, 286, 68, state.indexStep, C_CYAN);

    int forceWidth = constrain(state.forceBar, 0, 20) * 14;
    hudCanvas_.drawRect(20, 102, 282, 5, C_DIM);
    hudCanvas_.fillRect(21, 103, forceWidth, 3, C_GREEN);

    textAt(hudCanvas_, "Z", 43, 111, C_MAGENTA, 1, MC_DATUM);
    snprintf(line, sizeof(line), "%+.3f mm", state.zMillimeters);
    textAt(hudCanvas_, line, 55, 132, C_MAGENTA, 2, MC_DATUM);
    drawStepIndicator(hudCanvas_, 7, 108, state.zStep, C_MAGENTA);

    textAt(hudCanvas_, "RPM", 160, 111, C_TEXT, 1, MC_DATUM);
    snprintf(line, sizeof(line), "%ld/%lu", state.rpmSet,
             static_cast<unsigned long>(state.rpmActual));
    textAt(hudCanvas_, line, 160, 132, C_TEXT, 2, MC_DATUM);

    textAt(hudCanvas_, "FLOW", 268, 111, C_TEXT, 1, MC_DATUM);
    snprintf(line, sizeof(line), "%.1f", state.flow);
    textAt(hudCanvas_, line, 268, 132, C_TEXT, 2, MC_DATUM);
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

    if (stateChanged || now - lastHudMs_ >= 500)
    {
        lastHudMs_ = now;
        drawHud(state);
        pushHudCanvas();
    }

    lastStateVersion_ = state.version;
}
