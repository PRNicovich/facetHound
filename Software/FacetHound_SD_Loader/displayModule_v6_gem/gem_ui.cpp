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
    char right[32];
    snprintf(right, sizeof(right), "%s  W%.0f",
             state.linkAlive ? "LINK" : "WAIT", state.wheelIndex);

    const char* title = state.jobActive && state.jobTitle && state.jobTitle[0]
                            ? state.jobTitle : GemData::kTitle;
    textAt(gemCanvas_, title, 7, 6, C_TEXT, 2, TL_DATUM);
    textAt(gemCanvas_, right, 313, 6, state.linkAlive ? C_GREEN : C_DIM, 2, TR_DATUM);

    char facet[30];
    if (state.jobActive)
        snprintf(facet, sizeof(facet), "TIER %u  FACET %u",
                 state.jobTier, state.jobFacet);
    else
    {
        if (runtimeGemMesh().active() && selectedPlane_ < runtimeGemMesh().planes().size())
        {
            const auto& plane = runtimeGemMesh().planes()[selectedPlane_];
            snprintf(facet, sizeof(facet), "TIER %u  FACET %u", plane.tier, plane.facet);
        }
        else
        {
            const auto& plane = GemData::kPlanes[selectedPlane_];
            snprintf(facet, sizeof(facet), "TIER %u  FACET %u", plane.tier, plane.facet);
        }
    }
    textAt(gemCanvas_, facet, 313, 25, C_AMBER, 1, TR_DATUM);
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
        screenX_[i] = int16_t(lroundf(centerX + scale * x1));
        screenY_[i] = int16_t(lroundf(centerY - scale * y2));
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
            gemCanvas_.drawLine(
                screenX_[a], screenY_[a], screenX_[b], screenY_[b],
                selected ? C_GREEN : C_EDGE
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
    snprintf(line, sizeof(line), "TIP %7.2f deg", state.tipDegrees);
    textAt(hudCanvas_, line, 12, 9, C_YELLOW, 4, TL_DATUM);

    const char* facetLabel = nullptr;
    bool tableCut = false;
    if (state.jobActive)
    {
        facetLabel = state.jobFacetName;
        tableCut = fabsf(state.jobAngle) >= 89.995f;
    }
    else if (runtimeGemMesh().active() &&
             selectedPlane_ < runtimeGemMesh().planes().size())
    {
        const RuntimeMeshPlane& plane = runtimeGemMesh().planes()[selectedPlane_];
        facetLabel = plane.name;
        tableCut = fabsf(plane.tipDegrees) >= 89.995f;
    }
    if ((!facetLabel || !facetLabel[0]) && tableCut) facetLabel = "T";
    if (facetLabel && facetLabel[0])
    {
        const uint8_t font = strlen(facetLabel) <= 4 ? 4 : 2;
        textAt(hudCanvas_, facetLabel, 312, font == 4 ? 9 : 17,
               C_AMBER, font, TR_DATUM);
    }

    float actualTwist = state.targetTwist + state.twistError;
    snprintf(line, sizeof(line), "IDX %6.2f  ERR %+5.2f", actualTwist, state.twistError);
    textAt(hudCanvas_, line, 12, 39, C_CYAN, 2, TL_DATUM);

    int forceWidth = constrain(state.forceBar, 0, 20) * 14;
    hudCanvas_.drawRect(20, 62, 282, 7, C_DIM);
    hudCanvas_.fillRect(21, 63, forceWidth, 5, C_GREEN);

    snprintf(line, sizeof(line), "Z %+0.3f", state.zMillimeters);
    textAt(hudCanvas_, line, 8, 81, C_MAGENTA, 2, TL_DATUM);

    snprintf(line, sizeof(line), "RPM %ld/%lu", state.rpmSet,
             static_cast<unsigned long>(state.rpmActual));
    textAt(hudCanvas_, line, 160, 81, C_TEXT, 2, MC_DATUM);

    snprintf(line, sizeof(line), "FLOW %.1f", state.flow);
    textAt(hudCanvas_, line, 312, 81, C_TEXT, 2, TR_DATUM);

    if (state.jobActive)
        snprintf(line, sizeof(line), "TARGET %+.2f deg  IDX %.2f",
                 state.jobAngle, state.jobIndex);
    else
        snprintf(line, sizeof(line), "%s   motor:%d  pump:%d",
                 displayModeName(mode_), state.rpmDirection, state.flowDirection);
    textAt(hudCanvas_, line, 160, 121, C_DIM, 1, MC_DATUM);
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
