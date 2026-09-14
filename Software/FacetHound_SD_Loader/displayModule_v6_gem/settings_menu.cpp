#include "settings_menu.h"

#include <math.h>
#include <stdarg.h>
#include <strings.h>

namespace
{
constexpr uint16_t C_BG = TFT_BLACK;
constexpr uint16_t C_TEXT = 0xE71C;
constexpr uint16_t C_DIM = 0x6B6D;
constexpr uint16_t C_LINE = 0x2145;
constexpr uint16_t C_SELECT = 0x067F;
constexpr uint16_t C_SELECT_BG = 0x10E4;
constexpr uint16_t C_ACTIVE = 0x27E8;
constexpr uint8_t kVisibleRows = 6;
constexpr uint8_t kRootItems = 13;

const float kWheelIndexes[] = {
    1, 2, 6.2832f, 32, 40, 48, 60, 64, 72, 77, 80, 81, 88, 91,
    96, 98, 99, 100, 102, 104, 110, 120, 128, 144, 192, 256, 360, 400
};
constexpr uint8_t kWheelCount = sizeof(kWheelIndexes) / sizeof(kWheelIndexes[0]);
const uint8_t kResetSpacing[] = {4, 8, 12, 16, 32};
constexpr uint8_t kResetCount = sizeof(kResetSpacing) / sizeof(kResetSpacing[0]);
const float kEditSteps[] = {10.0f, 1.0f, 0.1f, 0.01f};
constexpr uint8_t kEditTierCount = sizeof(kEditSteps) / sizeof(kEditSteps[0]);
const float kFlowSteps[] = {0.01f, 0.001f, 0.0001f, 0.00001f};
constexpr uint8_t kFlowTierCount = sizeof(kFlowSteps) / sizeof(kFlowSteps[0]);
const float kSpinSteps[] = {1.0f, 0.1f, 0.01f};
constexpr uint8_t kSpinTierCount = sizeof(kSpinSteps) / sizeof(kSpinSteps[0]);

uint16_t wrapCursor(int value, uint16_t count)
{
    if (!count) return 0;
    while (value < 0) value += count;
    return uint16_t(value % count);
}

uint16_t nearestWheel(float value)
{
    uint16_t best = 0;
    float error = fabsf(value - kWheelIndexes[0]);
    for (uint16_t i = 1; i < kWheelCount; ++i)
    {
        float next = fabsf(value - kWheelIndexes[i]);
        if (next < error) { error = next; best = i; }
    }
    return best;
}
}  // namespace

SettingsMenu::SettingsMenu(TFT_eSPI& display) : tft_(display) {}

void SettingsMenu::setCommand(MenuResult& result, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(result.command, sizeof(result.command), format, args);
    va_end(args);
    result.commandReady = true;
}

void SettingsMenu::open(DisplayMode currentMode)
{
    open_ = true;
    currentMode_ = currentMode;
    rootCursor_ = 0;
    status_[0] = '\0';
    enterRootPage();
    draw();
}

void SettingsMenu::close() { open_ = false; }

void SettingsMenu::enterRootPage()
{
    page_ = Page::ROOT;
}

void SettingsMenu::enterChoicePage(Page page, uint16_t cursor)
{
    page_ = page;
    choiceCursor_ = cursor;
}

void SettingsMenu::invalidatePositionCache(uint32_t start)
{
    cacheStart_ = start;
    for (bool& valid : positionValid_) valid = false;
}

bool SettingsMenu::cachedPosition(uint32_t index, float* value) const
{
    if (index < cacheStart_ || index >= cacheStart_ + kCacheSize) return false;
    uint8_t slot = uint8_t(index - cacheStart_);
    if (!positionValid_[slot]) return false;
    if (value) *value = positionCache_[slot];
    return true;
}

void SettingsMenu::requestPositionWindow(MenuResult& result)
{
    if (positionCursor_ >= positionCount_) return;
    uint32_t start = (positionCursor_ / kCacheSize) * kCacheSize;
    if (start != cacheStart_) invalidatePositionCache(start);
    if (!cachedPosition(positionCursor_, nullptr))
        setCommand(result, "@CFGGET,POSITIONS,%lu,%u",
                   static_cast<unsigned long>(start), kCacheSize);
}

void SettingsMenu::invalidateSdCache(uint32_t start)
{
    sdCacheStart_ = start;
    for (uint8_t i = 0; i < kSdCacheSize; ++i)
    {
        sdFileValid_[i] = false;
        sdFileNames_[i][0] = '\0';
    }
}

void SettingsMenu::requestSdWindow(MenuResult& result)
{
    uint32_t start = (sdCursor_ / kSdCacheSize) * kSdCacheSize;
    if (start != sdCacheStart_) invalidateSdCache(start);
    uint8_t slot = uint8_t(sdCursor_ - sdCacheStart_);
    if (sdFileCount_ == 0 || slot >= kSdCacheSize || !sdFileValid_[slot])
        setCommand(result, "@CFGGET,SD_FILES,%lu,%u",
                   static_cast<unsigned long>(start), kSdCacheSize);
}

MenuResult SettingsMenu::handle(MenuKey key)
{
    MenuResult result;
    if (!open_) return result;

    if (page_ == Page::ROOT)
    {
        if (key == MenuKey::UP) rootCursor_ = wrapCursor(int(rootCursor_) - 1, kRootItems);
        else if (key == MenuKey::DOWN) rootCursor_ = wrapCursor(int(rootCursor_) + 1, kRootItems);
        else if (key == MenuKey::BACK)
        {
            close(); result.closed = true; return result;
        }
        else if (key == MenuKey::SELECT)
        {
            switch (rootCursor_)
            {
                case 0: enterChoicePage(Page::DISPLAY_MODE, uint16_t(currentMode_)); break;
                case 1: enterChoicePage(Page::INDEX_DIRECTION, indexSign_ < 0 ? 1 : 0); break;
                case 2: enterChoicePage(Page::TABLE_ADAPTER, tableAdapter_ ? 1 : 0); break;
                case 3: enterChoicePage(Page::SERVO_ENABLED, servoEnabled_ ? 1 : 0); break;
                case 4: enterChoicePage(Page::WHEEL_INDEX, nearestWheel(wheelIndex_)); break;
                case 5: enterChoicePage(Page::Z_POLARITY, zSign_ < 0 ? 1 : 0); break;
                case 6: page_ = Page::FLOW_CALIBRATION; flowEditTier_ = 0; break;
                case 7: enterChoicePage(Page::ENCODER_ZERO, 0); break;
                case 8: enterChoicePage(Page::SPECIAL_COMMANDS, 0); break;
                case 9: enterChoicePage(Page::RESET_POSITIONS, 0); break;
                case 10:
                    page_ = Page::SD_FILES;
                    sdCursor_ = 0;
                    snprintf(status_, sizeof(status_), "Reading SD card...");
                    invalidateSdCache(0);
                    requestSdWindow(result);
                    break;
                case 11:
                    page_ = Page::POSITIONS;
                    positionCursor_ = 0;
                    invalidatePositionCache(0);
                    requestPositionWindow(result);
                    break;
                default: close(); result.closed = true; return result;
            }
        }
    }
    else if (page_ == Page::POSITIONS)
    {
        uint32_t items = positionCount_ + 1; // final row is Add position
        if (key == MenuKey::BACK) enterRootPage();
        else if (key == MenuKey::UP)
            positionCursor_ = items ? (positionCursor_ + items - 1) % items : 0;
        else if (key == MenuKey::DOWN)
            positionCursor_ = items ? (positionCursor_ + 1) % items : 0;
        else if (key == MenuKey::DELETE_ITEM && positionCursor_ < positionCount_)
        {
            setCommand(result, "@CFGACTION,DELETE_POSITION,%lu",
                       static_cast<unsigned long>(positionCursor_));
            if (positionCount_) --positionCount_;
            if (positionCursor_ > positionCount_) positionCursor_ = positionCount_;
            invalidatePositionCache((positionCursor_ / kCacheSize) * kCacheSize);
        }
        else if (key == MenuKey::SELECT)
        {
            if (positionCursor_ == positionCount_)
            {
                setCommand(result, "@CFGACTION,ADD_POSITION,0");
                ++positionCount_;
                positionCursor_ = positionCount_ - 1;
                invalidatePositionCache((positionCursor_ / kCacheSize) * kCacheSize);
            }
            else
            {
                float value = 0.0f;
                if (cachedPosition(positionCursor_, &value))
                {
                    editPositionIndex_ = positionCursor_;
                    editPositionValue_ = value;
                    editTier_ = 1;
                    page_ = Page::POSITION_EDITOR;
                }
            }
        }
        if (page_ == Page::POSITIONS && !result.commandReady) requestPositionWindow(result);
    }
    else if (page_ == Page::POSITION_EDITOR)
    {
        if (key == MenuKey::UP || key == MenuKey::DOWN)
        {
            editPositionValue_ += (key == MenuKey::UP ? 1.0f : -1.0f) * kEditSteps[editTier_];
            while (editPositionValue_ < 0.0f) editPositionValue_ += wheelIndex_;
            while (editPositionValue_ >= wheelIndex_) editPositionValue_ -= wheelIndex_;
        }
        else if (key == MenuKey::FINER)
            editTier_ = editTier_ + 1 < kEditTierCount ? editTier_ + 1 : kEditTierCount - 1;
        else if (key == MenuKey::COARSER || key == MenuKey::BACK)
            editTier_ = editTier_ ? editTier_ - 1 : 0;
        else if (key == MenuKey::DELETE_ITEM)
        {
            setCommand(result, "@CFGACTION,DELETE_POSITION,%lu",
                       static_cast<unsigned long>(editPositionIndex_));
            if (positionCount_) --positionCount_;
            positionCursor_ = editPositionIndex_ < positionCount_ ? editPositionIndex_ : positionCount_;
            invalidatePositionCache((positionCursor_ / kCacheSize) * kCacheSize);
            page_ = Page::POSITIONS;
        }
        else if (key == MenuKey::SELECT)
        {
            setCommand(result, "@CFGSET,POSITION_%lu,%.4f",
                       static_cast<unsigned long>(editPositionIndex_), editPositionValue_);
            uint8_t slot = uint8_t(editPositionIndex_ - cacheStart_);
            if (slot < kCacheSize)
            {
                positionCache_[slot] = editPositionValue_;
                positionValid_[slot] = true;
            }
            page_ = Page::POSITIONS;
        }
    }
    else if (page_ == Page::FLOW_CALIBRATION)
    {
        if (key == MenuKey::UP || key == MenuKey::DOWN)
        {
            flowConversion_ += (key == MenuKey::UP ? 1.0f : -1.0f) * kFlowSteps[flowEditTier_];
            flowConversion_ = constrain(flowConversion_, 0.000001f, 10.0f);
        }
        else if (key == MenuKey::FINER)
            flowEditTier_ = flowEditTier_ + 1 < kFlowTierCount ? flowEditTier_ + 1 : kFlowTierCount - 1;
        else if (key == MenuKey::COARSER)
            flowEditTier_ = flowEditTier_ ? flowEditTier_ - 1 : 0;
        else if (key == MenuKey::BACK) enterRootPage();
        else if (key == MenuKey::SELECT)
        {
            setCommand(result, "@CFGSET,FLOW_CONVERSION,%.6f", flowConversion_);
            enterRootPage();
        }
    }
    else if (page_ == Page::INDEX_SPIN)
    {
        if (key == MenuKey::UP || key == MenuKey::DOWN)
        {
            indexSpinRpm_ += (key == MenuKey::UP ? 1.0f : -1.0f) * kSpinSteps[spinEditTier_];
            indexSpinRpm_ = constrain(indexSpinRpm_, -10.0f, 10.0f);
            if (fabsf(indexSpinRpm_) < 0.01f) indexSpinRpm_ = key == MenuKey::UP ? 0.01f : -0.01f;
            if (indexSpinRunning_)
                setCommand(result, "@CFGACTION,INDEX_SPIN_START,%.2f", indexSpinRpm_);
        }
        else if (key == MenuKey::FINER)
            spinEditTier_ = spinEditTier_ + 1 < kSpinTierCount ? spinEditTier_ + 1 : kSpinTierCount - 1;
        else if (key == MenuKey::COARSER)
            spinEditTier_ = spinEditTier_ ? spinEditTier_ - 1 : 0;
        else if (key == MenuKey::BACK)
        {
            indexSpinRunning_ = false;
            setCommand(result, "@CFGACTION,INDEX_SPIN_STOP,0");
            enterChoicePage(Page::SPECIAL_COMMANDS, 0);
        }
        else if (key == MenuKey::SELECT)
        {
            indexSpinRunning_ = !indexSpinRunning_;
            if (indexSpinRunning_)
                setCommand(result, "@CFGACTION,INDEX_SPIN_START,%.2f", indexSpinRpm_);
            else
                setCommand(result, "@CFGACTION,INDEX_SPIN_STOP,0");
        }
    }
    else if (page_ == Page::SD_FILES)
    {
        if (key == MenuKey::BACK) enterRootPage();
        else if (key == MenuKey::UP && sdFileCount_)
            sdCursor_ = (sdCursor_ + sdFileCount_ - 1) % sdFileCount_;
        else if (key == MenuKey::DOWN && sdFileCount_)
            sdCursor_ = (sdCursor_ + 1) % sdFileCount_;
        else if (key == MenuKey::SELECT && sdFileCount_)
        {
            snprintf(status_, sizeof(status_), "Loading file %lu...",
                     static_cast<unsigned long>(sdCursor_ + 1));
            setCommand(result, "@CFGACTION,LOAD_SD_FILE,%lu",
                       static_cast<unsigned long>(sdCursor_));
        }
        if (page_ == Page::SD_FILES && !result.commandReady) requestSdWindow(result);
    }
    else
    {
        uint16_t count = 2;
        if (page_ == Page::DISPLAY_MODE) count = 3;
        else if (page_ == Page::WHEEL_INDEX) count = kWheelCount;
        else if (page_ == Page::RESET_POSITIONS) count = kResetCount;
        else if (page_ == Page::ENCODER_ZERO) count = 4;
        else if (page_ == Page::SPECIAL_COMMANDS) count = 1;

        if (key == MenuKey::UP) choiceCursor_ = wrapCursor(int(choiceCursor_) - 1, count);
        else if (key == MenuKey::DOWN) choiceCursor_ = wrapCursor(int(choiceCursor_) + 1, count);
        else if (key == MenuKey::BACK) enterRootPage();
        else if (key == MenuKey::SELECT)
        {
            if (page_ == Page::DISPLAY_MODE)
            {
                currentMode_ = static_cast<DisplayMode>(choiceCursor_);
                result.modeSelected = true;
                result.selectedMode = currentMode_;
            }
            else if (page_ == Page::INDEX_DIRECTION)
            {
                indexSign_ = choiceCursor_ ? -1 : 1;
                setCommand(result, "@CFGSET,INDEX_DIRECTION,%s", indexSign_ < 0 ? "CCW" : "CW");
            }
            else if (page_ == Page::TABLE_ADAPTER)
            {
                tableAdapter_ = choiceCursor_ != 0;
                setCommand(result, "@CFGSET,TABLE_ADAPTER,%s", tableAdapter_ ? "ON" : "OFF");
            }
            else if (page_ == Page::SERVO_ENABLED)
            {
                servoEnabled_ = choiceCursor_ != 0;
                setCommand(result, "@CFGSET,SERVO_ENABLED,%s", servoEnabled_ ? "ON" : "OFF");
            }
            else if (page_ == Page::WHEEL_INDEX)
            {
                wheelIndex_ = kWheelIndexes[choiceCursor_];
                setCommand(result, "@CFGSET,WHEEL_INDEX,%.4f", wheelIndex_);
            }
            else if (page_ == Page::Z_POLARITY)
            {
                zSign_ = choiceCursor_ ? -1 : 1;
                setCommand(result, "@CFGSET,Z_POLARITY,%s", zSign_ < 0 ? "REVERSED" : "NORMAL");
            }
            else if (page_ == Page::ENCODER_ZERO)
            {
                const char* encoders[] = {"INDEX", "TIP", "Z", "ALL"};
                setCommand(result, "@CFGACTION,ZERO_ENCODER,%s", encoders[choiceCursor_]);
            }
            else if (page_ == Page::SPECIAL_COMMANDS)
            {
                page_ = Page::INDEX_SPIN;
                spinEditTier_ = 0;
                indexSpinRunning_ = false;
            }
            else if (page_ == Page::RESET_POSITIONS)
            {
                uint8_t spacing = kResetSpacing[choiceCursor_];
                positionCount_ = uint32_t(ceilf(wheelIndex_ / float(spacing)));
                invalidatePositionCache(0);
                setCommand(result, "@CFGACTION,RESET_POSITIONS,%u", spacing);
            }
            if (!result.modeSelected && page_ != Page::INDEX_SPIN) enterRootPage();
        }
    }

    draw();
    return result;
}

void SettingsMenu::applyConfig(const char* id, const char* value)
{
    if (!id || !value) return;
    if (!strcasecmp(id, "INDEX_DIRECTION")) indexSign_ = !strcasecmp(value, "CCW") ? -1 : 1;
    else if (!strcasecmp(id, "TABLE_ADAPTER")) tableAdapter_ = !strcasecmp(value, "ON");
    else if (!strcasecmp(id, "SERVO_ENABLED")) servoEnabled_ = !strcasecmp(value, "ON");
    else if (!strcasecmp(id, "WHEEL_INDEX")) wheelIndex_ = strtof(value, nullptr);
    else if (!strcasecmp(id, "Z_POLARITY")) zSign_ = !strcasecmp(value, "REVERSED") ? -1 : 1;
    else if (!strcasecmp(id, "FLOW_CONVERSION")) flowConversion_ = strtof(value, nullptr);
    else if (!strcasecmp(id, "INDEX_SPIN_RPM"))
    {
        float rpm = strtof(value, nullptr);
        indexSpinRunning_ = fabsf(rpm) >= 0.01f;
        if (indexSpinRunning_) indexSpinRpm_ = rpm;
    }
    else if (!strcasecmp(id, "INDEX_SPIN_START"))
    {
        float rpm = strtof(value, nullptr);
        bool changed = !indexSpinRunning_ || fabsf(indexSpinRpm_ - rpm) > 0.001f;
        indexSpinRunning_ = true;
        indexSpinRpm_ = rpm;
        if (changed && open_) draw();
        return;
    }
    else if (!strcasecmp(id, "INDEX_SPIN_STOP"))
    {
        bool changed = indexSpinRunning_;
        indexSpinRunning_ = false;
        if (changed && open_) draw();
        return;
    }
    else if (!strcasecmp(id, "ZERO_ENCODER"))
        snprintf(status_, sizeof(status_), "%s encoder zeroed", value);
    else if (!strcasecmp(id, "SD_STATUS"))
    {
        snprintf(sdStatus_, sizeof(sdStatus_), "%s", value);
        if (!strcasecmp(value, "NO_CARD"))
            snprintf(status_, sizeof(status_), "Insert a FAT32 SD card");
        else if (!strcasecmp(value, "READY") && !sdFileCount_)
            snprintf(status_, sizeof(status_), "No .asc or .fct files in root");
    }
    else if (!strcasecmp(id, "SD_ACTIVE"))
    {
        snprintf(activeDesign_, sizeof(activeDesign_), "%s", value);
    }
    else if (!strcasecmp(id, "SD_FILE_COUNT"))
    {
        sdFileCount_ = strtoul(value, nullptr, 10);
        if (sdCursor_ >= sdFileCount_) sdCursor_ = sdFileCount_ ? sdFileCount_ - 1 : 0;
        if (sdFileCount_) snprintf(status_, sizeof(status_), "%lu design file%s",
                                   static_cast<unsigned long>(sdFileCount_),
                                   sdFileCount_ == 1 ? "" : "s");
    }
    else if (!strncasecmp(id, "SD_FILE_", 8))
    {
        uint32_t index = strtoul(id + 8, nullptr, 10);
        if (index >= sdCacheStart_ && index < sdCacheStart_ + kSdCacheSize)
        {
            uint8_t slot = uint8_t(index - sdCacheStart_);
            snprintf(sdFileNames_[slot], sizeof(sdFileNames_[slot]), "%s", value);
            sdFileValid_[slot] = true;
        }
    }
    else if (!strcasecmp(id, "LOAD_SD_FILE"))
    {
        snprintf(activeDesign_, sizeof(activeDesign_), "%s", value);
        snprintf(status_, sizeof(status_), "Loaded: %.36s", value);
    }
    else if (!strcasecmp(id, "POSITION_COUNT"))
    {
        positionCount_ = strtoul(value, nullptr, 10);
        if (positionCursor_ > positionCount_) positionCursor_ = positionCount_;
    }
    else if (!strncasecmp(id, "POSITION_", 9))
    {
        uint32_t index = strtoul(id + 9, nullptr, 10);
        if (index >= cacheStart_ && index < cacheStart_ + kCacheSize)
        {
            uint8_t slot = uint8_t(index - cacheStart_);
            positionCache_[slot] = strtof(value, nullptr);
            positionValid_[slot] = true;
        }
    }
    if (open_) draw();
}

void SettingsMenu::applyError(const char* id, const char* reason)
{
    if (!id || !reason) return;
    if (!strcasecmp(id, "LOAD_SD_FILE"))
        snprintf(status_, sizeof(status_), "Load failed: %.30s", reason);
    else
        snprintf(status_, sizeof(status_), "%.16s: %.24s", id, reason);
    if (open_) draw();
}

void SettingsMenu::drawTitle(const char* subtitle)
{
    tft_.setTextDatum(TL_DATUM);
    tft_.setTextFont(4);
    tft_.setTextColor(C_TEXT, C_BG);
    tft_.drawString("SETTINGS", 18, 12);
    tft_.setTextFont(2);
    tft_.setTextColor(C_DIM, C_BG);
    tft_.drawString(subtitle, 19, 47);
    tft_.drawFastHLine(18, 72, 284, C_LINE);
}

void SettingsMenu::drawRow(int y, const char* label, const char* value,
                           bool selected, bool active)
{
    uint16_t bg = selected ? C_SELECT_BG : C_BG;
    uint16_t fg = selected ? C_SELECT : C_TEXT;
    tft_.fillRoundRect(14, y, 292, 48, 5, bg);
    if (selected) tft_.fillTriangle(21, y + 18, 21, y + 30, 28, y + 24, C_SELECT);
    tft_.setTextDatum(ML_DATUM);
    tft_.setTextFont(2);
    tft_.setTextColor(fg, bg);
    tft_.drawString(label, 36, y + 24);
    if (value && value[0])
    {
        tft_.setTextDatum(MR_DATUM);
        tft_.setTextColor(active ? C_ACTIVE : fg, bg);
        tft_.drawString(value, 292, y + 24);
    }
}

void SettingsMenu::drawFooter(const char* text)
{
    tft_.setTextDatum(BL_DATUM);
    tft_.setTextFont(1);
    tft_.setTextColor(C_DIM, C_BG);
    tft_.drawString(text, 17, 468);
}

void SettingsMenu::drawRoot()
{
    static const char* labels[kRootItems] = {
        "Display mode", "Index direction", "Table adapter", "Servo control",
        "Wheel index", "Z axis polarity", "Flow calibration", "Encoder zero",
        "Special commands", "Reset positions", "Load SD design", "Edit positions",
        "Close settings"
    };
    char values[kRootItems][18] = {};
    snprintf(values[0], sizeof(values[0]), "%s", displayModeName(currentMode_));
    snprintf(values[1], sizeof(values[1]), "%s", indexSign_ < 0 ? "CCW" : "CW");
    snprintf(values[2], sizeof(values[2]), "%s", tableAdapter_ ? "ON" : "OFF");
    snprintf(values[3], sizeof(values[3]), "%s", servoEnabled_ ? "ON" : "OFF");
    snprintf(values[4], sizeof(values[4]), "%.3g", wheelIndex_);
    snprintf(values[5], sizeof(values[5]), "%s", zSign_ < 0 ? "REVERSED" : "NORMAL");
    snprintf(values[6], sizeof(values[6]), "%.5f", flowConversion_);
    snprintf(values[10], sizeof(values[10]), "%.17s",
             strcasecmp(activeDesign_, "NONE") ? activeDesign_ : sdStatus_);
    snprintf(values[11], sizeof(values[11]), "%lu", static_cast<unsigned long>(positionCount_));

    drawTitle("machine and display configuration");
    uint16_t start = rootCursor_ >= kVisibleRows ? rootCursor_ - kVisibleRows + 1 : 0;
    for (uint8_t row = 0; row < kVisibleRows && start + row < kRootItems; ++row)
        drawRow(82 + row * 56, labels[start + row], values[start + row], rootCursor_ == start + row, true);
    drawFooter("TWIST WHEEL scroll/click   TOP-LEFT back");
}

void SettingsMenu::drawChoices()
{
    const char* subtitle = "choose value";
    uint16_t count = 2;
    if (page_ == Page::DISPLAY_MODE) { subtitle = "display mode"; count = 3; }
    else if (page_ == Page::INDEX_DIRECTION) subtitle = "positive index direction";
    else if (page_ == Page::TABLE_ADAPTER) subtitle = "subtract 45 degrees from tip";
    else if (page_ == Page::SERVO_ENABLED) subtitle = "automatic spin servo";
    else if (page_ == Page::WHEEL_INDEX) { subtitle = "wheel index"; count = kWheelCount; }
    else if (page_ == Page::Z_POLARITY) subtitle = "Z readout and motor direction";
    else if (page_ == Page::ENCODER_ZERO) { subtitle = "reset current position to zero"; count = 4; }
    else if (page_ == Page::SPECIAL_COMMANDS) { subtitle = "operations not mapped to keys"; count = 1; }
    else if (page_ == Page::RESET_POSITIONS) { subtitle = "position spacing on wheel"; count = kResetCount; }
    drawTitle(subtitle);

    uint16_t start = choiceCursor_ >= kVisibleRows ? choiceCursor_ - kVisibleRows + 1 : 0;
    for (uint8_t row = 0; row < kVisibleRows && start + row < count; ++row)
    {
        uint16_t index = start + row;
        char label[28] = {};
        char value[22] = {};
        if (page_ == Page::DISPLAY_MODE)
        {
            snprintf(label, sizeof(label), "%s", displayModeName(static_cast<DisplayMode>(index)));
            const char* detail[] = {"machine status", "moving gem", "four views"};
            snprintf(value, sizeof(value), "%s", detail[index]);
        }
        else if (page_ == Page::INDEX_DIRECTION) snprintf(label, sizeof(label), "%s", index ? "CCW" : "CW");
        else if (page_ == Page::TABLE_ADAPTER || page_ == Page::SERVO_ENABLED)
            snprintf(label, sizeof(label), "%s", index ? "ON" : "OFF");
        else if (page_ == Page::WHEEL_INDEX) snprintf(label, sizeof(label), "%.4g", kWheelIndexes[index]);
        else if (page_ == Page::Z_POLARITY) snprintf(label, sizeof(label), "%s", index ? "REVERSED" : "NORMAL");
        else if (page_ == Page::ENCODER_ZERO)
        {
            const char* labels[] = {"Zero index encoder", "Zero tip encoder", "Zero Z encoder", "Zero all encoders"};
            snprintf(label, sizeof(label), "%s", labels[index]);
        }
        else if (page_ == Page::SPECIAL_COMMANDS) snprintf(label, sizeof(label), "Index continuous spin");
        else snprintf(label, sizeof(label), "Every %u", kResetSpacing[index]);
        drawRow(82 + row * 56, label, value, choiceCursor_ == index);
    }
    drawFooter("TWIST WHEEL choose/click   TOP-LEFT cancel");
}

void SettingsMenu::drawPositions()
{
    drawTitle("numeric mark-point list");
    uint32_t items = positionCount_ + 1;
    uint32_t start = positionCursor_ >= kVisibleRows ? positionCursor_ - kVisibleRows + 1 : 0;
    for (uint8_t row = 0; row < kVisibleRows && start + row < items; ++row)
    {
        uint32_t index = start + row;
        char label[26] = {};
        char value[18] = {};
        if (index == positionCount_)
        {
            snprintf(label, sizeof(label), "+ Add position");
        }
        else
        {
            snprintf(label, sizeof(label), "Position %lu", static_cast<unsigned long>(index + 1));
            float position = 0.0f;
            if (cachedPosition(index, &position)) snprintf(value, sizeof(value), "%.4f", position);
            else snprintf(value, sizeof(value), "loading...");
        }
        drawRow(82 + row * 56, label, value, positionCursor_ == index, index < positionCount_);
    }
    drawFooter("CLICK edit/add   SERVO KEY delete   TOP-LEFT back");
}

void SettingsMenu::drawPositionEditor()
{
    drawTitle("edit mark point");
    char index[32];
    char value[32];
    char step[32];
    snprintf(index, sizeof(index), "Position %lu", static_cast<unsigned long>(editPositionIndex_ + 1));
    snprintf(value, sizeof(value), "%.4f", editPositionValue_);
    snprintf(step, sizeof(step), "Step %.2f", kEditSteps[editTier_]);
    drawRow(110, index, "", false);
    drawRow(174, "Value", value, true, true);
    drawRow(238, "Edit tier", step, false, true);
    drawFooter("WHEEL changes   TOP-LEFT coarse   SERVO KEY fine   CLICK save");
}

void SettingsMenu::drawFlowCalibration()
{
    drawTitle("mL/min displayed per flow tick");
    char value[24] = {};
    char step[24] = {};
    snprintf(value, sizeof(value), "%.6f", flowConversion_);
    snprintf(step, sizeof(step), "%.5f", kFlowSteps[flowEditTier_]);
    drawRow(126, "Conversion", value, true, true);
    drawRow(190, "Edit step", step, false, true);
    drawFooter("WHEEL changes   SERVO KEY fine   CLICK save   TOP-LEFT cancel");
}

void SettingsMenu::drawIndexSpin()
{
    drawTitle("constant open-loop index rotation");
    char rpm[24] = {};
    char step[24] = {};
    snprintf(rpm, sizeof(rpm), "%+.2f rpm", indexSpinRpm_);
    snprintf(step, sizeof(step), "%.2f rpm", kSpinSteps[spinEditTier_]);
    drawRow(110, "Signed rate", rpm, true, true);
    drawRow(174, "Edit step", step, false, true);
    drawRow(238, "Motor", indexSpinRunning_ ? "RUNNING" : "STOPPED", false, indexSpinRunning_);
    drawFooter(indexSpinRunning_ ? "CLICK stop   WHEEL changes live   TOP-LEFT stop/back"
                                 : "CLICK start   +/- chooses direction   TOP-LEFT back");
}

void SettingsMenu::drawSdFiles()
{
    char subtitle[56] = {};
    snprintf(subtitle, sizeof(subtitle), "active: %.42s", activeDesign_);
    drawTitle(subtitle);

    if (!strcasecmp(sdStatus_, "NO_CARD"))
    {
        drawRow(150, "No SD card", "check wiring/card", true);
    }
    else if (!sdFileCount_)
    {
        drawRow(150, "No design files", ".asc or .fct", true);
    }
    else
    {
        uint32_t start = (sdCursor_ / kSdCacheSize) * kSdCacheSize;
        for (uint8_t row = 0; row < kVisibleRows && start + row < sdFileCount_; ++row)
        {
            uint32_t index = start + row;
            char label[48] = {};
            if (index >= sdCacheStart_ && index < sdCacheStart_ + kSdCacheSize &&
                sdFileValid_[index - sdCacheStart_])
                snprintf(label, sizeof(label), "%.43s", sdFileNames_[index - sdCacheStart_]);
            else
                snprintf(label, sizeof(label), "loading...");
            drawRow(82 + row * 56, label, "", sdCursor_ == index);
        }
    }
    drawFooter(status_[0] ? status_ : "WHEEL choose   CLICK load   TOP-LEFT back");
}

void SettingsMenu::draw()
{
    if (!open_) return;
    tft_.fillScreen(C_BG);
    if (page_ == Page::ROOT) drawRoot();
    else if (page_ == Page::POSITIONS) drawPositions();
    else if (page_ == Page::POSITION_EDITOR) drawPositionEditor();
    else if (page_ == Page::FLOW_CALIBRATION) drawFlowCalibration();
    else if (page_ == Page::INDEX_SPIN) drawIndexSpin();
    else if (page_ == Page::SD_FILES) drawSdFiles();
    else drawChoices();
}
