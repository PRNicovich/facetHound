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

uint16_t orderedFacetForPlane(const RuntimeGemMesh& runtime,
                              uint16_t planeIndex)
{
    const bool runtimeActive = runtime.active();
    const uint16_t count = runtimeActive ? uint16_t(runtime.planes().size())
                                         : GemData::kPlaneCount;
    if (planeIndex >= count) return 0;
    const float resolution = runtimeActive ? runtime.indexResolution()
                                           : float(GemData::kIndexResolution);
    auto planeTier = [&](uint16_t i) {
        return runtimeActive ? runtime.planes()[i].tier : GemData::kPlanes[i].tier;
    };
    auto machineIndex = [&](uint16_t i) {
        const float tip = runtimeActive ? runtime.planes()[i].tipDegrees
                                        : GemData::kPlanes[i].tipDegrees;
        float index = runtimeActive ? runtime.planes()[i].twistTicks
                                    : GemData::kPlanes[i].twistTicks;
        if (oppositeApproach(tip)) index += resolution * 0.5f;
        index = fmodf(index, resolution);
        return index < 0.0f ? index + resolution : index;
    };

    const uint16_t tier = planeTier(planeIndex);
    const float index = machineIndex(planeIndex);
    uint16_t rank = 1;
    for (uint16_t j = 0; j < count; ++j) {
        if (j == planeIndex || planeTier(j) != tier) continue;
        const float other = machineIndex(j);
        if (other < index - 1.0e-4f ||
            (fabsf(other - index) <= 1.0e-4f && j < planeIndex))
            ++rank;
    }
    return rank;
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

void GemUi::releaseSprites()
{
    gemCanvas_.deleteSprite();
    hudCanvas_.deleteSprite();
    gemCanvasReady_ = hudCanvasReady_ = false;
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
    orientationInitialized_ = false;
    lastStateVersion_ = UINT32_MAX;
    lastHudVersion_ = UINT32_MAX;
    lastStaticVersion_ = UINT32_MAX;
    lastStaticLinkAlive_ = false;
    lastFrameMs_ = 0;
    lastHudMs_ = 0;
    tipHistoryCount_ = 0;
    lastTipSample_ = UINT32_MAX;

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
    hudCanvas_.resetViewport();
    hudCanvas_.setTextSize(1);
    hudCanvas_.setTextWrap(false,false);
    return true;
}

void GemUi::invalidate()
{
    lastHudVersion_ = UINT32_MAX;
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

void GemUi::fillSelectedProjection(uint16_t edgeCount, uint8_t panel)
{
    const RuntimeGemMesh& runtime = runtimeGemMesh();
    uint16_t vertices[64] = {};
    float angles[64] = {};
    uint8_t count = 0;
    float depth = 0.0f;
    for (uint16_t i = 0; i < edgeCount && count < 64; ++i) {
        if (!edgeSelected(i, selectedPlane_)) continue;
        if (panel < 2) {
            if (runtime.active()) {
                if (!runtime.edgeVisibleFromCap(i,panel==0)) continue;
            } else if ((GemData::kEdges[i].viewMask & (1u << panel)) == 0) {
                continue;
            }
        }
        const uint16_t ends[2] = {
            uint16_t(runtime.active() ? runtime.edges()[i].a : GemData::kEdges[i].a),
            uint16_t(runtime.active() ? runtime.edges()[i].b : GemData::kEdges[i].b)};
        for (uint16_t end : ends) {
            bool present = false;
            for (uint8_t j = 0; j < count; ++j) present |= vertices[j] == end;
            if (!present && count < 64) {
                vertices[count++] = end;
                depth += screenDepth_[end];
            }
        }
    }
    if (count < 3) return;
    int32_t cx = 0, cy = 0;
    for (uint8_t i = 0; i < count; ++i) {
        cx += screenX_[vertices[i]];
        cy += screenY_[vertices[i]];
    }
    cx /= count; cy /= count;
    for (uint8_t i = 0; i < count; ++i)
        angles[i] = atan2f(screenY_[vertices[i]] - cy, screenX_[vertices[i]] - cx);
    for (uint8_t i = 1; i < count; ++i) {
        const uint16_t vertex = vertices[i];
        const float angle = angles[i];
        int8_t j = i - 1;
        while (j >= 0 && angles[j] > angle) {
            vertices[j + 1] = vertices[j]; angles[j + 1] = angles[j]; --j;
        }
        vertices[j + 1] = vertex; angles[j + 1] = angle;
    }
    const bool behind = panel >= 2 && selectedBehind(panel);
    const uint16_t fill = behind ? 0x0102 : 0x0204;
    for (uint8_t i = 0; i < count; ++i) {
        const uint16_t a = vertices[i], b = vertices[(i + 1) % count];
        gemCanvas_.fillTriangle(cx, cy, screenX_[a], screenY_[a],
                                screenX_[b], screenY_[b], fill);
    }
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

    // Facet numbers shown to the operator are always the ascending-index
    // ordinal. The protocol's facet ID remains untouched so it still selects
    // the exact plane supplied by the source design.
    const uint16_t orderedFacet = orderedFacetForPlane(runtime, selectedPlane_);
    if (orderedFacet) facet = orderedFacet;

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
    float targetTip = state.jobActive && isfinite(state.jobAngle)
                          ? machineTip(state.jobAngle) : 45.0f;
    float targetTwist = normalizedTwist(
        state.actualTwistValid ? state.actualTwist-(state.cheatReceived?state.cheatIndex:0) : displayedTwist_ * state.wheelIndex / meshResolution,
        state.wheelIndex
    );

    int jobPlane = -1;
    if (state.jobActive) {
        if (runtimeGemMesh().active()) {
            jobPlane = runtimeGemMesh().findPlane(state.jobTier, state.jobFacet);
        } else {
            for (uint16_t i = 0; i < GemData::kPlaneCount; ++i)
                if (GemData::kPlanes[i].tier == state.jobTier &&
                    GemData::kPlanes[i].facet == state.jobFacet) { jobPlane = i; break; }
        }
    }
    // No matching identity => no highlight, never guess from sensor values.
    selectedPlane_ = jobPlane >= 0 ? uint16_t(jobPlane) : UINT16_MAX;
    const auto& loaded = runtimeGemMesh();
    if (loaded.active() && selectedPlane_ < loaded.planes().size() &&
        loaded.planes()[selectedPlane_].normalValid) {
        const auto& face = loaded.planes()[selectedPlane_];
        if (!orientationInitialized_ || state.indexEngaged) {
            runtimeTipTarget_ = atan2f(hypotf(face.nx, face.ny), face.nz) / DEG_TO_RAD;
            runtimeRollTarget_ = face.nz > 0.02f ? 180.0f : 0.0f;
            const float faceAzimuth = atan2f(face.ny, face.nx);
            float machineTarget = selectedTargetTwist(state);
            runtimeTwistOffset_ = (0.25f - faceAzimuth / TWO_PI) * meshResolution +
                loaded.indexSign() * machineTarget;
        }
        float actual = targetTwist; // normalizedTwist already converted to mesh units.
        float twistTarget = normalizedTwist(runtimeTwistOffset_ - loaded.indexSign()*actual, meshResolution);
        uint32_t now = millis();
        float gain = 1.0f-expf(-float(min(uint32_t(100), now-lastPoseMs_))/65.0f);
        lastPoseMs_ = now;
        if (!orientationInitialized_) {
            displayedRenderTip_ = runtimeTipTarget_; displayedRenderTwist_ = twistTarget;
            displayedRoll_ = runtimeRollTarget_;
        } else {
            displayedRoll_ += wrappedDelta(runtimeRollTarget_,displayedRoll_,360)*gain;
            displayedRenderTip_ += wrappedDelta(runtimeTipTarget_, displayedRenderTip_, 360)*gain;
            displayedRenderTwist_ = normalizedTwist(displayedRenderTwist_ +
                wrappedDelta(twistTarget, displayedRenderTwist_, meshResolution)*gain, meshResolution);
        }
        orientationInitialized_ = true;
        return;
    }

    // Selection is always live. Unlocked means highlight-only: freeze the
    // rendered orientation, including any unfinished interpolation. Seed the
    // first frame once so startup still has a meaningful view.
    // Unlocked selection changes only the highlight. Physical encoder motion
    // remains visible even when unlocked (including continuous index spin).
    // Locked: actual index drives rotation; selected tier drives inclination.
    // Convert machine wheel units to the mesh's index resolution.
    displayedTip_ = targetTip;
    displayedTwist_ = state.wheelIndex > 0.0f
        ? targetTwist * meshResolution / state.wheelIndex : 0.0f;
    poseInitialized_ = true;

    float storedSelectedTip = 0.0f;
    const RuntimeGemMesh& runtime = runtimeGemMesh();
    if (runtime.active() && selectedPlane_ < runtime.planes().size())
        storedSelectedTip = runtime.planes()[selectedPlane_].tipDegrees;
    else if (selectedPlane_ < GemData::kPlaneCount)
        storedSelectedTip = GemData::kPlanes[selectedPlane_].tipDegrees;
    if (!orientationInitialized_ || state.indexEngaged) poseTierTip_ = storedSelectedTip;
    storedSelectedTip = poseTierTip_;
    displayedTip_ = machineTip(storedSelectedTip);
    const bool reverseView = oppositeApproach(storedSelectedTip) ||
                             fabsf(fabsf(storedSelectedTip) - 90.0f) <= 0.05f;
    const float baseRenderTip = displayedTip_ *
                                (oppositeApproach(storedSelectedTip) ? 1.0f : -1.0f);
    // Ry(180) * Rx(tip) * Rz(index) has the equivalent two-axis form
    // Rx(180-tip) * Rz(index+180). Folding the front-facing correction into
    // the pose lets index interpolation retain its normal shortest-path rule.
    const float renderTipTarget = reverseView ? 180.0f - baseRenderTip
                                               : baseRenderTip;
    const float renderTwistTarget = normalizedTwist(
        displayedTwist_ + (reverseView ? meshResolution * 0.5f : 0.0f),
        meshResolution);
    const uint32_t now = millis();
    const float dt = min(uint32_t(100), now - lastPoseMs_);
    lastPoseMs_ = now;
    const float gain = 1.0f - expf(-dt / 65.0f);
    if (!orientationInitialized_) {
        displayedRenderTip_ = renderTipTarget;
        displayedRenderTwist_ = renderTwistTarget;
    } else {
        displayedRenderTip_ += wrappedDelta(renderTipTarget, displayedRenderTip_, 360.0f) * gain;
        displayedRenderTwist_ = normalizedTwist(displayedRenderTwist_ +
            wrappedDelta(renderTwistTarget, displayedRenderTwist_, meshResolution) * gain,
            meshResolution);
    }
    orientationInitialized_ = true;
}

void GemUi::drawHeader(const GemTelemetry& state, bool showTier)
{
    const char* title = state.jobActive && state.jobTitle && state.jobTitle[0]
                            ? state.jobTitle : GemData::kTitle;
    const char* cursor = title;
    for (int row=0; row<2 && *cursor; ++row) {
        size_t count = min(size_t(23), strlen(cursor));
        if (cursor[count] && row==0) {
            size_t split=count; while (split && cursor[split]!=' ') --split;
            if (split) count=split;
        }
        char line[26] = {}; memcpy(line,cursor,count);
        if (row==1 && cursor[count] && count>=3) memcpy(line+count-3,"...",3);
        textAt(gemCanvas_,line,7,5+row*11,C_NAME,1,TL_DATUM);
        cursor+=count; while (*cursor==' ') ++cursor;
    }

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
    const float tip = displayedRenderTip_ * DEG_TO_RAD;
    const float twist = displayedRenderTwist_ * TWO_PI / resolution;
    const float ct = cosf(tip), st = sinf(tip);
    const float cz = cosf(twist), sz = sinf(twist);
    const float roll = runtime.active() ? displayedRoll_*DEG_TO_RAD : 0.0f;
    const float cr=cosf(roll), sr=sinf(roll);
    const float scale = 100.0f / (runtime.active() ? runtime.radius() : GemData::kRadius);
    constexpr float centerX = 117.0f;
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
        screenX_[i] = int16_t(lroundf(centerX + scale * (cr*x1-sr*y2)));
        screenY_[i] = int16_t(lroundf(centerY - scale * (sr*x1+cr*y2)));
        screenDepth_[i] = z2;
    }

    // A dark green convex fan reads as a translucent selected face on this
    // non-alpha framebuffer; all wireframe edges are redrawn over it below.
    uint16_t faceVertices[64] = {};
    float faceAngles[64] = {};
    uint8_t faceCount = 0;
    const uint16_t selectedEdgeCount = runtime.active()
                                           ? uint16_t(runtime.edges().size())
                                           : GemData::kEdgeCount;
    for (uint16_t i = 0; i < selectedEdgeCount && faceCount < 64; ++i) {
        if (!edgeSelected(i, selectedPlane_)) continue;
        const uint16_t ends[2] = {
            uint16_t(runtime.active() ? runtime.edges()[i].a : GemData::kEdges[i].a),
            uint16_t(runtime.active() ? runtime.edges()[i].b : GemData::kEdges[i].b)};
        for (uint16_t end : ends) {
            bool present = false;
            for (uint8_t j = 0; j < faceCount; ++j) present |= faceVertices[j] == end;
            if (!present && faceCount < 64) faceVertices[faceCount++] = end;
        }
    }
    if (faceCount >= 3) {
        int32_t centerFaceX = 0, centerFaceY = 0;
        for (uint8_t i = 0; i < faceCount; ++i) {
            centerFaceX += screenX_[faceVertices[i]];
            centerFaceY += screenY_[faceVertices[i]];
        }
        centerFaceX /= faceCount;
        centerFaceY /= faceCount;
        for (uint8_t i = 0; i < faceCount; ++i)
            faceAngles[i] = atan2f(screenY_[faceVertices[i]] - centerFaceY,
                                   screenX_[faceVertices[i]] - centerFaceX);
        for (uint8_t i = 1; i < faceCount; ++i) {
            const uint16_t vertex = faceVertices[i];
            const float angle = faceAngles[i];
            int8_t j = i - 1;
            while (j >= 0 && faceAngles[j] > angle) {
                faceVertices[j + 1] = faceVertices[j];
                faceAngles[j + 1] = faceAngles[j];
                --j;
            }
            faceVertices[j + 1] = vertex;
            faceAngles[j + 1] = angle;
        }
        constexpr uint16_t C_FACE_FILL = 0x0204;
        for (uint8_t i = 0; i < faceCount; ++i) {
            const uint16_t a = faceVertices[i];
            const uint16_t b = faceVertices[(i + 1) % faceCount];
            gemCanvas_.fillTriangle(centerFaceX, centerFaceY, screenX_[a], screenY_[a],
                                    screenX_[b], screenY_[b], C_FACE_FILL);
        }
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
    drawHeader(state,true);
    const auto& mesh=runtimeGemMesh();
    const bool loaded=mesh.active();
    const uint16_t nv=loaded?mesh.vertices().size():GemData::kVertexCount;
    const uint16_t ne=loaded?mesh.edges().size():GemData::kEdgeCount;
    auto xAt=[&](uint16_t i){return loaded?mesh.vertices()[i].x:GemData::kVertices[i].x;};
    auto yAt=[&](uint16_t i){return loaded?mesh.vertices()[i].y:GemData::kVertices[i].y;};
    auto zAt=[&](uint16_t i){return loaded?mesh.vertices()[i].z:GemData::kVertices[i].z;};
    // The cap follows selected tier; S is a fixed projection, never auto-rotated.
    bool pavilion=oppositeApproach(state.jobAngle) ||
        (fabsf(state.jobAngle)<0.000001f && (signbit(state.jobAngle) || state.jobGemcadDistance<0));
    if(loaded && selectedPlane_<mesh.planes().size() && mesh.planes()[selectedPlane_].normalValid &&
       fabsf(mesh.planes()[selectedPlane_].nz)>0.00001f)
        pavilion=mesh.planes()[selectedPlane_].nz<0;
    const uint8_t cap=pavilion?1:0;
    for(uint8_t view=0;view<2;++view) {
        const uint8_t panel=view?3:cap;
        const int bx=10,by=view?194:34,bw=218,bh=view?120:154;
        float minU=INFINITY,maxU=-INFINITY,minV=INFINITY,maxV=-INFINITY;
        for(uint16_t i=0;i<nv;++i) {
            const float u=view?yAt(i):xAt(i);
            const float v=view?zAt(i):(cap?-yAt(i):yAt(i));
            minU=min(minU,u);maxU=max(maxU,u);minV=min(minV,v);maxV=max(maxV,v);
        }
        const float scale=min((bw-20)/max(maxU-minU,1e-6f),(bh-20)/max(maxV-minV,1e-6f));
        for(uint16_t i=0;i<nv;++i) {
            const float u=view?yAt(i):xAt(i);
            const float v=view?zAt(i):(cap?-yAt(i):yAt(i));
            screenX_[i]=lroundf(bx+bw*.5f+(u-(minU+maxU)*.5f)*scale);
            screenY_[i]=lroundf(by+bh*.5f-(v-(minV+maxV)*.5f)*scale);
            screenDepth_[i]=view?xAt(i):(cap?-zAt(i):zAt(i));
        }
        fillSelectedProjection(ne,panel);
        const bool faceBehind=view && selectedBehind(panel);
        for(uint8_t pass=0;pass<2;++pass) for(uint16_t e=0;e<ne;++e) {
            if(!view && !(loaded?mesh.edgeVisibleFromCap(e,cap==0):
                (GemData::kEdges[e].viewMask&(1u<<cap))!=0)) continue;
            const bool selected=edgeSelected(e,selectedPlane_);
            if(selected!=(pass==1)) continue;
            const uint16_t a=loaded?mesh.edges()[e].a:GemData::kEdges[e].a;
            const uint16_t b=loaded?mesh.edges()[e].b:GemData::kEdges[e].b;
            const bool behind=view && (selected?faceBehind:(screenDepth_[a]+screenDepth_[b]<0));
            const uint16_t color=selected?(behind?0x1384:C_GREEN):(behind?C_DIM:C_EDGE);
            gemCanvas_.drawLine(screenX_[a],screenY_[a],screenX_[b],screenY_[b],color);
        }
        textAt(gemCanvas_,view?"S":cap?"B":"T",bx,by+3,C_NAME,2,TL_DATUM);
    }
}

bool GemUi::selectedBehind(uint8_t panel) const
{
    const auto& mesh=runtimeGemMesh();
    if(mesh.active() && selectedPlane_<mesh.planes().size() &&
       mesh.planes()[selectedPlane_].normalValid) {
        const auto& n=mesh.planes()[selectedPlane_];
        return panel==3?n.nx<0:panel==2?n.ny>0:false;
    }
    // Built-in convex faces: recover the outward normal from actual vertices.
    float ax=0,ay=0,az=0,nx=0,ny=0,nz=0,best=0;
    bool anchor=false;
    for(uint16_t e=0;e<GemData::kEdgeCount && !mesh.active();++e) {
        if(!edgeSelected(e,selectedPlane_))continue;
        const auto& a=GemData::kVertices[GemData::kEdges[e].a];
        const auto& b=GemData::kVertices[GemData::kEdges[e].b];
        if(!anchor){ax=a.x;ay=a.y;az=a.z;anchor=true;}
        const float ux=a.x-ax,uy=a.y-ay,uz=a.z-az;
        const float vx=b.x-ax,vy=b.y-ay,vz=b.z-az;
        const float cx=uy*vz-uz*vy,cy=uz*vx-ux*vz,cz=ux*vy-uy*vx;
        const float length=cx*cx+cy*cy+cz*cz;
        if(length>best){best=length;nx=cx;ny=cy;nz=cz;}
    }
    if(nx*ax+ny*ay+nz*az<0){nx=-nx;ny=-ny;}
    return panel==3?nx<0:panel==2?ny>0:false;
}

void GemUi::drawHud(const GemTelemetry& state)
{
    hudCanvas_.fillSprite(C_BG);
    hudCanvas_.drawFastHLine(8,0,304,C_PANEL);
    // Decimal anchored fields: three whole digits reserved; Z has three decimals.
    auto number=[&](float v,int digits,int x,int y,uint8_t font,uint16_t color,bool sign=false) {
        char buffer[32];snprintf(buffer,sizeof(buffer),sign?"%+.*f":"%.*f",digits,v);
        char* dot=strchr(buffer,'.'); if(!dot)return;
        textAt(hudCanvas_,dot,x,y,color,font,BL_DATUM);
        *dot=0;textAt(hudCanvas_,buffer,x,y,color,font,BR_DATUM);
    };
    const float tipError=state.tipDegrees-selectedTargetTip(state);
    const float nominal=state.cheatReceived?state.nominalIndex:
        selectedTargetTwist(state)*state.wheelIndex/
        (runtimeGemMesh().active()?runtimeGemMesh().indexResolution():float(GemData::kIndexResolution));
    const float actual=state.actualTwistValid?state.actualTwist:state.targetTwist+state.twistError;
    const float indexError=wrappedDelta(actual,state.targetTwist,state.wheelIndex);
    number(state.tipDegrees,2,111,43,4,C_YELLOW);
    drawDegreeGlyph(hudCanvas_,163,21,C_YELLOW);
    // Explicit delta triangle avoids missing custom-font Unicode glyphs.
    hudCanvas_.drawTriangle(198,29,193,40,203,40,C_YELLOW);
    number(tipError,2,270,43,2,C_YELLOW,true);
    drawDegreeGlyph(hudCanvas_,305,28,C_YELLOW);

    number(nominal,2,111,81,4,C_CYAN);
    const uint16_t cheatColor=state.cheatTemporary?TFT_WHITE:C_CYAN;
    textAt(hudCanvas_,"CHEAT",176,62,cheatColor,2,BL_DATUM);
    number(state.cheatReceived?state.cheatIndex:0,2,270,62,2,cheatColor,true);
    hudCanvas_.drawTriangle(198,69,193,80,203,80,C_CYAN);
    number(indexError,2,270,82,2,C_CYAN,true);

    number(state.zMillimeters,3,111,119,4,C_MAGENTA);
    textAt(hudCanvas_,"mm",180,119,C_MAGENTA,2,BL_DATUM);
    drawSmallAxisSymbol(hudCanvas_,14,31,true,C_YELLOW);
    drawSmallAxisSymbol(hudCanvas_,14,69,false,C_CYAN);
    textAt(hudCanvas_,"z",14,108,C_MAGENTA,4,MC_DATUM);
    if(state.indexEngaged)hudCanvas_.fillCircle(38,58,4,C_CYAN);
    else hudCanvas_.drawCircle(38,58,4,C_DIM);
    drawStepIndicator(hudCanvas_,32,68,state.indexStep,C_CYAN);
    if(state.zEngaged)hudCanvas_.fillCircle(38,96,4,C_MAGENTA);
    else hudCanvas_.drawCircle(38,96,4,C_DIM);
    drawStepIndicator(hudCanvas_,32,106,state.zStep,C_MAGENTA);

    const char* fault=nullptr;
    if(!state.linkAlive)fault="LINK LOST";
    else if(state.faults&1)fault="ENC STALE";
    else if(state.faults&2)fault="INDEX FAULT";
    else if(state.faults&4)fault="LAP FAULT";
    else if(state.faults&8)fault="Z STALE";
    if(fault)textAt(hudCanvas_,fault,314,115,TFT_RED,2,BR_DATUM);

    const bool running=state.rpmDirection==1 || state.rpmDirection==3;
    const bool clockwise=state.rpmDirection==1 || state.rpmDirection==2;
    const uint16_t rpmColor=running?TFT_WHITE:C_DIM;
    drawRotationArrow(hudCanvas_,17,143,clockwise,running,rpmColor);
    char value[24];snprintf(value,sizeof(value),"%lu",static_cast<unsigned long>(running?state.rpmActual:max(0L,state.rpmSet)));
    textAt(hudCanvas_,value,99,157,rpmColor,4,BR_DATUM);
    textAt(hudCanvas_,"rpm",103,155,rpmColor,2,BL_DATUM);
    const bool flowing=state.flowDirection==0 || state.flowDirection==2;
    drawWaterDrop(hudCanvas_,166,143,flowing?C_CYAN:C_DIM);
    snprintf(value,sizeof(value),"%.1f",state.flow);
    textAt(hudCanvas_,value,239,157,flowing?TFT_WHITE:C_DIM,4,BR_DATUM);
    textAt(hudCanvas_,"mL/min",245,155,flowing?TFT_WHITE:C_DIM,1,BL_DATUM);
}

void GemUi::drawErrorScale()
{
    // Only this strip changes for new tip readings in Static mode.
    gemCanvas_.fillRect(238,34,82,286,C_BG);
    auto yAt=[](float error) {
        float a=fabsf(error);
        float d=a<=.05f?a/.05f:1+log10f(a/.05f);
        d=min(d,1+log10f(200.0f));
        return int(lroundf(184-copysignf(d,error)*106/(1+log10f(200.0f))));
    };
    gemCanvas_.drawFastVLine(274,78,213,C_DIM);
    for(int sign=-1;sign<=1;sign+=2) {
        for(float tick:{.1f,1.0f,10.0f}) {
            int y=yAt(sign*tick);
            gemCanvas_.drawFastHLine(270,y,8,C_NAME);
            char label[12];snprintf(label,sizeof(label),"%s%g",sign>0?"+":"-",tick);
            textAt(gemCanvas_,label,282,y,C_NAME,1,ML_DATUM);
        }
        for(float tick:{.05f,.5f,5.0f})
            gemCanvas_.drawFastHLine(272,yAt(sign*tick),5,C_DIM);
    }
    gemCanvas_.drawLine(269,73,274,66,C_NAME);gemCanvas_.drawLine(274,66,279,73,C_NAME);
    gemCanvas_.drawFastVLine(274,66,12,C_NAME);
    gemCanvas_.drawLine(269,295,274,302,C_NAME);gemCanvas_.drawLine(274,302,279,295,C_NAME);
    gemCanvas_.drawFastVLine(274,290,12,C_NAME);
    gemCanvas_.drawLine(284,179,278,184,TFT_YELLOW);
    gemCanvas_.drawLine(278,184,284,189,TFT_YELLOW);
    textAt(gemCanvas_,"0",291,184,C_YELLOW,2,ML_DATUM);
    // Build per-pixel intensity so overlapping aged marks add light.
    float intensity[213]={};
    for(uint8_t i=0;i+1<tipHistoryCount_;++i) {
        const int y=yAt(tipHistory_[i])-78;
        const float alpha=.60f*powf(.76f,tipHistoryCount_-2-i);
        if(y>=0 && y<213)intensity[y]=1-(1-intensity[y])*(1-alpha);
    }
    for(int y=0;y<213;++y) if(intensity[y]>0) {
        uint8_t r=uint8_t(31*intensity[y]),g=uint8_t(63*intensity[y]);
        gemCanvas_.fillRect(247,77+y,22,3,uint16_t(r<<11)|uint16_t(g<<5));
    }
    if(tipHistoryCount_)gemCanvas_.fillRect(247,yAt(tipHistory_[tipHistoryCount_-1])-1,22,3,TFT_YELLOW);
    gemCanvas_.drawTriangle(253,307,249,316,257,316,C_YELLOW);
    drawSmallAxisSymbol(gemCanvas_,270,312,true,C_YELLOW);
    drawDegreeGlyph(gemCanvas_,284,307,C_YELLOW);
}

void GemUi::pushGemCanvas()
{
    if (gemCanvasReady_) {drawErrorScale(); gemCanvas_.pushSprite(0, 0);}
}

void GemUi::pushHudCanvas()
{
    if (hudCanvasReady_) hudCanvas_.pushSprite(0, kGemHeight);
}

void GemUi::tick(const GemTelemetry& state)
{
    const uint32_t now = millis();
    if (!gemCanvasReady_ || !hudCanvasReady_) {
        // A loading sprite may have temporarily exhausted RAM at transition.
        // Retry after it is released rather than leaving a permanently blank HUD.
        static uint32_t lastAllocationRetry=0;
        if(now-lastAllocationRetry<500) return;
        lastAllocationRetry=now;
        if(!begin(mode_)) return;
    }
    const bool stateChanged = state.version != lastStateVersion_;
    const bool selectionChanged = state.jobTier != lastJobTier_ ||
                                  state.jobFacet != lastJobFacet_;
    bool gemPushed=false;

    if(selectionChanged)tipHistoryCount_=0;
    const bool newTip=state.tipSampleSequence!=lastTipSample_;
    if(newTip) {
        lastTipSample_=state.tipSampleSequence;
        const float error=state.tipDegrees-selectedTargetTip(state);
        if(state.linkAlive && !(state.faults&1) && isfinite(error)) {
            if(tipHistoryCount_==10) {
                for(int i=1;i<10;++i)tipHistory_[i-1]=tipHistory_[i];
                --tipHistoryCount_;
            }
            tipHistory_[tipHistoryCount_++]=error;
        } else tipHistoryCount_=0;
    }
    if (mode_ == DisplayMode::DYNAMIC)
    {
        if (selectionChanged || now - lastFrameMs_ >= kFramePeriodMs)
        {
            lastFrameMs_ = now;
            const float oldTip=displayedRenderTip_,oldTwist=displayedRenderTwist_,oldRoll=displayedRoll_;
            updatePose(state);
            if(selectionChanged || stateChanged ||
               fabsf(displayedRenderTip_-oldTip)>0.002f || fabsf(displayedRenderTwist_-oldTwist)>0.002f ||
               fabsf(displayedRoll_-oldRoll)>0.002f) {
                drawDynamic(state);
                pushGemCanvas();
                gemPushed=true;
            }
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
            gemPushed=true;
        }

        lastStaticVersion_ = state.version;
        lastStaticLinkAlive_ = state.linkAlive;
    }

    if((newTip || selectionChanged) && !gemPushed) {
        drawErrorScale();
        gemCanvas_.pushSprite(238,34,238,34,82,286);
    }

    if (lastHudVersion_ == UINT32_MAX || selectionChanged || (state.version != lastHudVersion_ && now - lastHudMs_ >= kHudPeriodMs) ||
        now - lastHudMs_ >= 500)
    {
        lastHudMs_ = now;
        lastHudVersion_ = state.version;
        drawHud(state);
        pushHudCanvas();
    }

    lastStateVersion_ = state.version;
    lastJobTier_ = state.jobTier;
    lastJobFacet_ = state.jobFacet;
}
