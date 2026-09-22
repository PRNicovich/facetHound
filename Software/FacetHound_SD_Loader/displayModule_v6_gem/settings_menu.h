#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "display_mode.h"

enum class MenuKey : uint8_t
{
    UP,
    DOWN,
    SELECT,
    BACK,
    FINER,
    COARSER,
    DELETE_ITEM,
};

struct MenuResult
{
    bool closed = false;
    bool modeSelected = false;
    DisplayMode selectedMode = DisplayMode::CLASSIC;
    bool commandReady = false;
    char command[96] = {};
};

class SettingsMenu
{
public:
    explicit SettingsMenu(TFT_eSPI& display);

    void open(DisplayMode currentMode);
    void openGemPicker() {
        page_=Page::SD_FILES;sdCursor_=0;invalidateSdCache(0);
        snprintf(status_,sizeof(status_),"Choose a gem or the built-in");
        draw();
    }
    void close();
    bool isOpen() const { return open_; }
    bool isGemPicker() const { return open_ && page_==Page::SD_FILES; }
    bool indexSpinRunning() const { return indexSpinRunning_; }
    float indexSpinRpm() const { return indexSpinRpm_; }
    MenuResult handle(MenuKey key);
    void applyConfig(const char* id, const char* value);
    void applyError(const char* id, const char* reason);
    void draw();
    void setConfigSync(bool active) {
        configSync_=active;
        if(!active && open_) refreshVisiblePage();
    }

private:
    bool configSync_=false;
    enum class Page : uint8_t
    {
        ROOT,
        DISPLAY_MODE,
        INDEX_DIRECTION,
        TABLE_ADAPTER,
        SERVO_ENABLED,
        WHEEL_INDEX,
        Z_POLARITY,
        FLOW_CALIBRATION,
        ENCODER_ZERO,
        SPECIAL_COMMANDS,
        INDEX_SPIN,
        RESET_POSITIONS,
        SD_FILES,
        POSITIONS,
        POSITION_EDITOR,
        GEM_INFO,
    };

    static constexpr uint8_t kCacheSize = 8;
    static constexpr uint8_t kSdCacheSize = 6;

    TFT_eSPI& tft_;
    bool open_ = false;
    Page page_ = Page::ROOT;
    uint16_t rootCursor_ = 0;
    uint16_t choiceCursor_ = 0;
    DisplayMode currentMode_ = DisplayMode::CLASSIC;
    int indexSign_ = 1;
    bool tableAdapter_ = false;
    bool servoEnabled_ = false;
    float wheelIndex_ = 96.0f;
    int zSign_ = 1;
    float flowConversion_ = 0.052f;
    uint8_t flowEditTier_ = 0;
    float indexSpinRpm_ = 1.0f;
    uint8_t spinEditTier_ = 0;
    bool indexSpinRunning_ = false;
    uint32_t positionCount_ = 0;
    uint32_t positionCursor_ = 0;
    uint32_t editPositionIndex_ = 0;
    float editPositionValue_ = 0.0f;
    uint8_t editTier_ = 1;
    uint32_t cacheStart_ = 0;
    float positionCache_[kCacheSize] = {};
    bool positionValid_[kCacheSize] = {};
    uint32_t sdFileCount_ = 0;
    uint32_t sdCursor_ = 0;
    uint32_t sdCacheStart_ = 0;
    char sdFileNames_[kSdCacheSize][48] = {};
    char sdFileTitles_[kSdCacheSize][48] = {};
    bool sdFileValid_[kSdCacheSize] = {};
    char sdStatus_[24] = "UNKNOWN";
    char sdDirectory_[192] = "/";
    char activeDesign_[48] = "NONE";
    char status_[48] = {};
    char gemInfo_[12][80] = {};
    void drawGemInfo();

    static void setCommand(MenuResult& result, const char* format, ...);
    void invalidatePositionCache(uint32_t start);
    bool cachedPosition(uint32_t index, float* value) const;
    void requestPositionWindow(MenuResult& result);
    void invalidateSdCache(uint32_t start);
    void requestSdWindow(MenuResult& result);
    void enterRootPage();
    void enterChoicePage(Page page, uint16_t cursor);

    void drawTitle(const char* subtitle);
    void drawRow(int y, const char* label, const char* value, bool selected,
                 bool active = false);
    void drawFooter(const char* text);
    void drawRootRow(uint16_t index, uint8_t row);
    void drawChoiceRow(uint16_t index, uint8_t row);
    void drawPositionRow(uint32_t index, uint8_t row);
    void drawSdRow(uint32_t index, uint8_t row);
    void redrawAfterInput(Page oldPage, uint16_t oldRoot, uint16_t oldChoice,
                          uint32_t oldPosition, uint32_t oldSd);
    void refreshVisiblePage();
    void drawRoot();
    void drawChoices();
    void drawPositions();
    void drawPositionEditor();
    void drawFlowCalibration();
    void drawIndexSpin();
    void drawSdFiles();
};
