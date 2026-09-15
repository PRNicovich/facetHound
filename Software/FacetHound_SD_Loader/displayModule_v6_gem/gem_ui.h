#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "display_mode.h"
#include "runtime_gem_mesh.h"

struct GemTelemetry
{
    float targetTwist = 0.0f;
    float twistError = 0.0f;
    float tipDegrees = 0.0f;
    float zMillimeters = 0.0f;
    float flow = 0.0f;
    float wheelIndex = 96.0f;
    uint32_t rpmActual = 0;
    long rpmSet = 0;
    int forceBar = 0;
    int rpmDirection = 1;
    int flowDirection = 1;
    int indexStep = 1;
    int zStep = 0;
    bool jobActive = false;
    const char* jobTitle = nullptr;
    const char* jobFacetName = nullptr;
    uint16_t jobTier = 0;
    uint16_t jobFacet = 0;
    float jobAngle = 0.0f;
    float jobGemcadDistance = 0.0f; // metadata only; never machine Z
    float jobIndex = 0.0f;
    bool linkAlive = false;
    uint32_t version = 0;
};

class GemUi
{
public:
    explicit GemUi(TFT_eSPI& display);
    bool begin(DisplayMode mode);
    void tick(const GemTelemetry& state);
    void invalidate();

private:
    static constexpr int kGemHeight = 330;
    static constexpr int kHudHeight = 150;
    static constexpr uint32_t kFramePeriodMs = 40;
    static constexpr uint32_t kHudPeriodMs = 80;

    TFT_eSPI& tft_;
    TFT_eSprite gemCanvas_;
    TFT_eSprite hudCanvas_;
    DisplayMode mode_ = DisplayMode::DYNAMIC;
    bool gemCanvasReady_ = false;
    bool hudCanvasReady_ = false;
    bool poseInitialized_ = false;
    float displayedTip_ = 0.0f;
    float displayedTwist_ = 0.0f;
    uint16_t selectedPlane_ = 0;
    uint32_t lastFrameMs_ = 0;
    uint32_t lastHudMs_ = 0;
    uint32_t lastStateVersion_ = UINT32_MAX;
    uint32_t lastStaticVersion_ = UINT32_MAX;
    bool lastStaticLinkAlive_ = false;
    int16_t screenX_[RUNTIME_MESH_MAX_VERTICES] = {};
    int16_t screenY_[RUNTIME_MESH_MAX_VERTICES] = {};
    float screenDepth_[RUNTIME_MESH_MAX_VERTICES] = {};

    float normalizedTwist(float ticks, float wheelIndex) const;
    float wrappedDelta(float target, float current, float period) const;
    uint16_t nearestPlane(float tipDegrees, float twistTicks) const;
    bool edgeSelected(uint16_t edgeIndex, uint16_t plane) const;
    void inferredTierName(uint16_t plane, char* text, size_t size) const;
    float selectedTargetTip(const GemTelemetry& state) const;
    float selectedTargetTwist(const GemTelemetry& state) const;
    void formatTierFacet(const GemTelemetry& state, char* text, size_t size,
                         bool compact = false) const;
    void updatePose(const GemTelemetry& state);
    void drawHeader(const GemTelemetry& state, bool showTier);
    void drawDynamic(const GemTelemetry& state);
    void drawStatic(const GemTelemetry& state);
    void drawHud(const GemTelemetry& state);
    void pushGemCanvas();
    void pushHudCanvas();
};
