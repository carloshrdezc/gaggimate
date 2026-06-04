#include "DefaultUI.h"

#include <WiFi.h>
#include <display/core/Controller.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/Process.h>
#include <display/core/zones.h>
#include <display/drivers/AmoledDisplayDriver.h>
#include <display/drivers/LilyGoDriver.h>
#include <display/drivers/WaveshareDriver.h>
#include <display/drivers/common/LV_Helper.h>
#include <display/main.h>
#include <display/ui/default/lvgl/gm_ui.h>
#include <display/ui/default/lvgl/ui_theme_manager.h>
#include <display/ui/default/lvgl/ui_themes.h>
#include <display/ui/utils/effects.h>

#include <cmath>

#include "esp_sntp.h"

static EffectManager effect_mgr;

namespace {
constexpr lv_opa_t OPA_45 = static_cast<lv_opa_t>(45);
constexpr lv_opa_t OPA_55 = static_cast<lv_opa_t>(55);
constexpr lv_opa_t OPA_185 = static_cast<lv_opa_t>(185);
constexpr lv_opa_t OPA_190 = static_cast<lv_opa_t>(190);
constexpr lv_opa_t OPA_200 = static_cast<lv_opa_t>(200);
constexpr lv_opa_t OPA_210 = static_cast<lv_opa_t>(210);
constexpr lv_opa_t OPA_220 = static_cast<lv_opa_t>(220);
} // namespace

int16_t calculate_angle(int set_temp, int range, int offset) {
    const double percentage = static_cast<double>(set_temp) / static_cast<double>(MAX_TEMP);
    return (percentage * ((double)range)) - range / 2 - offset;
}

void DefaultUI::updateTempHistory() {
    if (currentTemp > 0) {
        tempHistory[tempHistoryIndex] = currentTemp;
        tempHistoryIndex += 1;
    }

    if (tempHistoryIndex > TEMP_HISTORY_LENGTH) {
        tempHistoryIndex = 0;
        isTempHistoryInitialized = true;
    }

    if (tempHistoryIndex % 4 == 0) {
        heatingFlash = !heatingFlash;
        rerender = true;
    }
}

void DefaultUI::updateTempStableFlag() {
    if (isTempHistoryInitialized) {
        float totalError = 0.0f;
        float maxError = 0.0f;
        for (uint16_t i = 0; i < TEMP_HISTORY_LENGTH; i++) {
            float error = abs(tempHistory[i] - targetTemp);
            totalError += error;
            maxError = error > maxError ? error : maxError;
        }

        const float avgError = totalError / TEMP_HISTORY_LENGTH;
        const float errorMargin = max(2.0f, static_cast<float>(targetTemp) * 0.02f);

        isTemperatureStable = avgError < errorMargin && maxError <= errorMargin;
    }

    // instantly reset stability if setpoint has changed
    if (prevTargetTemp != targetTemp) {
        isTemperatureStable = false;
    }

    prevTargetTemp = targetTemp;
}

void DefaultUI::adjustHeatingIndicator(lv_obj_t *dials) {
    lv_obj_t *heatingIcon = ui_comp_get_child(dials, UI_COMP_DIALS_TEMPICON);
    lv_obj_set_style_img_recolor(heatingIcon, lv_color_hex(isTemperatureStable ? 0x00D100 : 0xF62C2C),
                                 LV_PART_MAIN | LV_STATE_DEFAULT);
    if (!isTemperatureStable) {
        lv_obj_set_style_opa(heatingIcon, heatingFlash ? LV_OPA_50 : LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void DefaultUI::reloadProfiles() { profileLoaded = 0; }

DefaultUI::DefaultUI(Controller *controller, Driver *driver, PluginManager *pluginManager)
    : controller(controller), panelDriver(driver), pluginManager(pluginManager) {
    setupPanel();
}

namespace {
struct DisplayPalette {
    lv_color_t surface;
    lv_color_t surfaceStrong;
    lv_color_t surfaceElevated;
    lv_color_t surfaceOutline;
    lv_color_t ringTrack;
    lv_color_t standby;
    lv_color_t accent;
    lv_color_t accentCool;
    lv_color_t water;
    lv_color_t grind;
    lv_color_t success;
    lv_color_t warning;
    lv_color_t danger;
    lv_color_t textPrimary;
    lv_color_t textMuted;
    lv_color_t textDisabled;
};

struct RingVisual {
    int value;
    lv_color_t tone;
    const char *title;
};

struct RingVisualContext {
    int mode;
    int currentTemperature;
    int targetTemperature;
    bool active;
    bool grindActive;
    Controller *controller;
    bool temperatureStable;
};

constexpr int MIN_STEAM_TARGET_C = 120;
constexpr int DEFAULT_STEAM_TARGET_C = 150;
constexpr int DEFAULT_WATER_TARGET_C = 80;
constexpr int STANDBY_REFERENCE_TARGET_C = 93;

int resolveDisplayThemeMode(const int requestedThemeMode, const bool amoledPanel) {
    if (amoledPanel) {
        return UI_THEME_DEFAULT;
    }

    return requestedThemeMode == UI_THEME_LIGHT ? UI_THEME_LIGHT : UI_THEME_DEFAULT;
}

DisplayPalette makeDisplayPalette(const int themeMode, const bool amoledPanel) {
    const int resolvedThemeMode = resolveDisplayThemeMode(themeMode, amoledPanel);

    if (resolvedThemeMode == UI_THEME_DEFAULT) {
        return {
            lv_color_hex(0x050505),
            lv_color_hex(0x0B0B0B),
            lv_color_hex(0x111111),
            lv_color_hex(0x2A2A2A),
            lv_color_hex(0x1E1E1E),
            lv_color_hex(0x333333),
            lv_color_hex(0xD71921),
            lv_color_hex(0xD4A843),
            lv_color_hex(0x6699CC),
            lv_color_hex(0x7CB876),
            lv_color_hex(0x7CB876),
            lv_color_hex(0xD4A843),
            lv_color_hex(0xD71921),
            lv_color_hex(0xFFFFFF),
            lv_color_hex(0x999999),
            lv_color_hex(0x666666),
        };
    }

    return {
        lv_color_hex(0xFFFFFF),
        lv_color_hex(0xFAFAFA),
        lv_color_hex(0xF5F5F5),
        lv_color_hex(0xDADADA),
        lv_color_hex(0xEDEDED),
        lv_color_hex(0xCCCCCC),
        lv_color_hex(0xD71921),
        lv_color_hex(0xD4A843),
        lv_color_hex(0x6699CC),
        lv_color_hex(0x7CB876),
        lv_color_hex(0x7CB876),
        lv_color_hex(0xD4A843),
        lv_color_hex(0xD71921),
        lv_color_hex(0x000000),
        lv_color_hex(0x1A1A1A),
        lv_color_hex(0x666666),
    };
}

int clampPercent(const double value) {
    if (!std::isfinite(value))
        return 0;
    if (value < 0.0)
        return 0;
    if (value > 100.0)
        return 100;
    return static_cast<int>(round(value));
}

int temperatureProgress(const int currentTemperature, const int targetTemperature) {
    if (targetTemperature <= 0)
        return 0;
    return clampPercent(static_cast<double>(currentTemperature) / static_cast<double>(targetTemperature) * 100.0);
}

lv_color_t modeTone(const DisplayPalette &palette, const int mode) {
    switch (mode) {
    case MODE_BREW:
        return palette.accent;
    case MODE_STEAM:
        return palette.accentCool;
    case MODE_WATER:
        return palette.water;
    case MODE_GRIND:
        return palette.grind;
    case MODE_STANDBY:
    default:
        return palette.standby;
    }
}

RingVisual buildRingVisual(const DisplayPalette &palette, const RingVisualContext &context) {
    if (context.mode == MODE_BREW) {
        ProcessSnapshot proc = context.controller->getProcessSnapshot();
        if (proc.exists && proc.isBrew) {
            if (proc.isActive) {
                int progress = 0;
                if (proc.target == ProcessTarget::VOLUMETRIC && proc.hasVolumetricTarget && proc.volumetricTargetValue > 0.0f) {
                    progress = clampPercent((proc.currentVolume / proc.volumetricTargetValue) * 100.0);
                } else if (proc.phaseDuration > 0 && proc.currentPhaseStarted > 0) {
                    progress = clampPercent((static_cast<double>(millis() - proc.currentPhaseStarted) /
                                             static_cast<double>(proc.phaseDuration)) *
                                            100.0);
                }
                return {progress, palette.success, "BREWING"};
            }
            if (proc.finished > 0) {
                return {100, palette.success, "FINISHED"};
            }
        }
    }

    if (context.mode == MODE_STEAM) {
        const int steamTarget =
            context.targetTemperature > MIN_STEAM_TARGET_C ? context.targetTemperature : DEFAULT_STEAM_TARGET_C;
        return {temperatureProgress(context.currentTemperature, steamTarget), palette.accentCool,
                context.currentTemperature < steamTarget ? "PREHEATING" : "STEAM"};
    }

    if (context.active) {
        return {100, modeTone(palette, context.mode), "ACTIVE"};
    }

    switch (context.mode) {
    case MODE_BREW:
        return {temperatureProgress(context.currentTemperature, context.targetTemperature), palette.accent,
                context.temperatureStable ? "BREW" : "HEATING"};
    case MODE_STEAM: {
        const int steamTarget =
            context.targetTemperature > MIN_STEAM_TARGET_C ? context.targetTemperature : DEFAULT_STEAM_TARGET_C;
        return {temperatureProgress(context.currentTemperature, steamTarget), palette.accentCool,
                context.currentTemperature < steamTarget ? "PREHEATING" : "STEAM"};
    }
    case MODE_WATER:
        return {temperatureProgress(context.currentTemperature,
                                    context.targetTemperature > 0 ? context.targetTemperature : DEFAULT_WATER_TARGET_C),
                palette.water, "WATER"};
    case MODE_GRIND:
        return {context.grindActive ? 100 : 8, palette.grind, context.grindActive ? "GRINDING" : "GRIND"};
    case MODE_STANDBY:
    default:
        return {temperatureProgress(context.currentTemperature, STANDBY_REFERENCE_TARGET_C), palette.standby, "STANDBY"};
    }
}

void stylePanel(lv_obj_t *obj, const DisplayPalette &palette, const lv_opa_t opa = LV_OPA_80, const lv_coord_t radius = 28) {
    if (obj == nullptr || !lv_obj_is_valid(obj))
        return;
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, palette.surfaceStrong, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(obj, palette.surfaceElevated, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, palette.surfaceOutline, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, LV_OPA_60, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(obj, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void styleChip(lv_obj_t *obj, const DisplayPalette &palette, const lv_color_t tone, const bool subtle = false) {
    if (obj == nullptr || !lv_obj_is_valid(obj))
        return;
    lv_obj_set_style_text_color(obj, tone, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, subtle ? palette.surfaceElevated : tone, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, subtle ? OPA_190 : LV_OPA_40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, tone, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 999, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(obj, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(obj, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(obj, 7, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(obj, 7, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, &ndot_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void styleHeadline(lv_obj_t *obj, const DisplayPalette &palette, const bool emphasis = false) {
    if (obj == nullptr || !lv_obj_is_valid(obj))
        return;
    lv_obj_set_style_text_color(obj, palette.textPrimary, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(obj, emphasis ? LV_OPA_COVER : LV_OPA_90, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(obj, emphasis ? 1 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, emphasis ? &ndot_24 : &ndot_18, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void styleSecondary(lv_obj_t *obj, const DisplayPalette &palette) {
    if (obj == nullptr || !lv_obj_is_valid(obj))
        return;
    lv_obj_set_style_text_color(obj, palette.textMuted, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(obj, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, &ndot_18, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void styleMetricValue(lv_obj_t *obj, const DisplayPalette &palette, const lv_font_t *font = &lv_font_montserrat_24) {
    if (obj == nullptr || !lv_obj_is_valid(obj))
        return;
    lv_obj_set_style_text_color(obj, palette.textPrimary, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void styleGlassButton(lv_obj_t *obj, const DisplayPalette &palette, const lv_color_t tone, const lv_coord_t radius,
                      const lv_coord_t borderWidth = 1, const lv_opa_t bgOpa = OPA_210) {
    if (obj == nullptr || !lv_obj_is_valid(obj))
        return;
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, palette.surfaceStrong, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(obj, palette.surfaceElevated, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, bgOpa, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, tone, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, borderWidth, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(obj, tone, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(obj, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void styleIconButton(lv_obj_t *obj, const DisplayPalette &palette, const lv_color_t tone) {
    if (obj == nullptr || !lv_obj_is_valid(obj))
        return;
    styleGlassButton(obj, palette, tone, 18);
    lv_obj_set_style_pad_left(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor(obj, tone, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(obj, palette.accent, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(obj, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void styleRoundIconButton(lv_obj_t *obj, const DisplayPalette &palette, const lv_color_t tone, const lv_coord_t size,
                          const bool prominent = false) {
    if (obj == nullptr || !lv_obj_is_valid(obj))
        return;
    lv_obj_set_size(obj, size, size);
    styleGlassButton(obj, palette, tone, 999, prominent ? 2 : 1, prominent ? OPA_200 : OPA_210);
    const lv_coord_t pad = size >= 72 ? 16 : 12;
    lv_obj_set_style_pad_all(obj, pad, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor(obj, tone, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void alignTopBackButton(lv_obj_t *obj, const DisplayPalette &palette, const lv_color_t tone) {
    styleRoundIconButton(obj, palette, tone, 52);
    lv_obj_align(obj, LV_ALIGN_TOP_LEFT, 28, 28);
}

void alignFooterAction(lv_obj_t *obj, const DisplayPalette &palette, const lv_color_t tone, const lv_coord_t y = -18,
                       const lv_coord_t size = 72) {
    styleRoundIconButton(obj, palette, tone, size, true);
    lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, y);
}

void styleMetricIcon(lv_obj_t *obj, const lv_color_t tone, const lv_coord_t zoom = 150) {
    if (obj == nullptr || !lv_obj_is_valid(obj))
        return;
    lv_obj_set_size(obj, 34, 34);
    lv_img_set_zoom(obj, zoom);
    lv_obj_set_style_img_recolor(obj, tone, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void styleFixedLabel(lv_obj_t *obj, const lv_coord_t width, const lv_coord_t height, const lv_font_t *font,
                     const lv_color_t color) {
    if (obj == nullptr || !lv_obj_is_valid(obj))
        return;
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void alignMetricPair(lv_obj_t *icon, lv_obj_t *label, const lv_coord_t centerX, const lv_coord_t centerY, const lv_color_t tone,
                     const DisplayPalette &palette) {
    styleMetricIcon(icon, tone);
    styleFixedLabel(label, 82, 28, &ndot_24, palette.textPrimary);
    lv_obj_align(icon, LV_ALIGN_CENTER, centerX - 38, centerY);
    lv_obj_align(label, LV_ALIGN_CENTER, centerX + 18, centerY);
}

// CAR-308: styleMenuTile() removed. The Nothing-theme mode launcher
// (ui_ModeScreen) styles its own tiles via gm_* builders + GM_* tokens
// directly in ui_ModeScreen_screen_init; no shared restyle helper needed.

void styleScreenBase(lv_obj_t *screen, const DisplayPalette &palette, const bool roundDisplay) {
    if (screen == nullptr || !lv_obj_is_valid(screen))
        return;
    lv_obj_set_style_bg_color(screen, palette.surface, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(screen, palette.surface, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(screen, palette.surfaceOutline, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(screen, roundDisplay ? LV_OPA_30 : LV_OPA_0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(screen, roundDisplay ? 2 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void setButtonLabel(lv_obj_t *button, lv_obj_t *label, const char *text, const lv_color_t tone) {
    if (button == nullptr || label == nullptr || !lv_obj_is_valid(button) || !lv_obj_is_valid(label))
        return;
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, tone, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &ndot_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(label, LV_OPA_0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(label, 92, 22);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_move_foreground(label);
}

void styleDialRing(lv_obj_t *arc, const DisplayPalette &palette, const lv_color_t tone, const bool roundDisplay,
                   const bool ambient) {
    if (arc == nullptr || !lv_obj_is_valid(arc))
        return;
    // Clear any image source so solid arc_color takes effect
    lv_obj_set_style_arc_img_src(arc, NULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_img_src(arc, NULL, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(arc, roundDisplay ? 16 : 22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(arc, roundDisplay ? 18 : 24, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(arc, ambient ? palette.surfaceOutline : palette.ringTrack, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(arc, ambient ? LV_OPA_30 : LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(arc, tone, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
}

void applyProcessRing(lv_obj_t *arc, const DisplayPalette &palette, const RingVisual &visual, const bool roundDisplay) {
    if (arc == nullptr || !lv_obj_is_valid(arc))
        return;
    styleDialRing(arc, palette, visual.tone, roundDisplay, false);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_bg_angles(arc, 210, 150);
    lv_arc_set_value(arc, visual.value);
}

String buildContextLine(const String &profile, const String &bean) {
    if (bean.isEmpty()) {
        return profile;
    }
    return profile + " • " + bean;
}
} // namespace

void DefaultUI::init() {
    profileManager = controller->getProfileManager();
    auto triggerRender = [this](Event const &) { rerender = true; };
    pluginManager->on("boiler:currentTemperature:change", [=](Event const &event) {
        int newTemp = static_cast<int>(event.getFloat("value"));
        if (newTemp != currentTemp) {
            currentTemp = newTemp;
            rerender = true;
        }
    });
    pluginManager->on("boiler:pressure:change", [=](Event const &event) {
        float newPressure = event.getFloat("value");
        if (round(newPressure * 10.0f) != round(pressure * 10.0f)) {
            pressure = newPressure;
            rerender = true;
        }
    });
    pluginManager->on("boiler:targetTemperature:change", [=](Event const &event) {
        int newTemp = static_cast<int>(event.getFloat("value"));
        if (newTemp != targetTemp) {
            targetTemp = newTemp;
            rerender = true;
        }
    });
    pluginManager->on("controller:targetVolume:change", [=](Event const &event) {
        targetVolume = event.getFloat("value");
        rerender = true;
    });
    pluginManager->on("controller:targetDuration:change", [=](Event const &event) {
        targetDuration = event.getFloat("value");
        rerender = true;
    });
    pluginManager->on("controller:grindDuration:change", [=](Event const &event) {
        grindDuration = event.getInt("value");
        rerender = true;
    });
    pluginManager->on("controller:grindVolume:change", [=](Event const &event) {
        grindVolume = event.getFloat("value");
        rerender = true;
    });
    pluginManager->on("controller:process:end", triggerRender);
    pluginManager->on("controller:process:start", triggerRender);
    pluginManager->on("controller:mode:change", [this](Event const &event) {
        mode = event.getInt("value");
        switch (mode) {
        case MODE_STANDBY:
            changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init);
            break;
        case MODE_BREW:
            changeScreen(&ui_BrewScreen, &ui_BrewScreen_screen_init);
            break;
        case MODE_GRIND:
            changeScreen(&ui_GrindScreen, &ui_GrindScreen_screen_init);
            break;
        // CAR-292: STEAM and WATER share ui_StatusScreen with the brew path.
        // gm_status_apply_mode() retints + relayouts per mode; updateStatusScreen()
        // drives hero/kicker/metrics directly from class state, so idle entry
        // (no live shot) renders the per-mode static layout cleanly.
        case MODE_STEAM:
            changeScreen(&ui_StatusScreen, &ui_StatusScreen_screen_init);
            break;
        case MODE_WATER:
            changeScreen(&ui_StatusScreen, &ui_StatusScreen_screen_init);
            break;
        default:
            break;
        };
    });
    pluginManager->on("controller:brew:start",
                      [this](Event const &event) { changeScreen(&ui_StatusScreen, &ui_StatusScreen_screen_init); });
    pluginManager->on("controller:brew:clear", [this](Event const &event) {
        if (lv_scr_act() == ui_StatusScreen) {
            changeScreen(&ui_BrewScreen, &ui_BrewScreen_screen_init);
        }
    });
    pluginManager->on("controller:brew:end", [this](Event const &) {
        if (autoSteamEnabled) {
            pendingAutoSteam = true;
            rerender = true;
        }
    });
    pluginManager->on("controller:bluetooth:waiting", [this](Event const &) {
        waitingForController = true;
        rerender = true;
    });
    pluginManager->on("controller:bluetooth:connect", [this](Event const &) {
        waitingForController = false;
        rerender = true;
        initialized = true;
        if (lv_scr_act() == ui_StandbyScreen) {
            Settings &settings = controller->getSettings();
            if (settings.getStartupMode() == MODE_BREW) {
                changeScreen(&ui_BrewScreen, &ui_BrewScreen_screen_init);
            } else {
                standbyEnterTime = millis();
            }
        }
        pressureAvailable = controller->getSystemInfo().capabilities.pressure;
    });
    pluginManager->on("controller:bluetooth:disconnect", [this](Event const &) {
        waitingForController = true;
        rerender = true;
    });
    pluginManager->on("controller:wifi:connect", [this](Event const &event) {
        rerender = true;
        apActive = event.getInt("AP");
    });
    pluginManager->on("ota:update:start", [this](Event const &) {
        updateActive = true;
        rerender = true;
        changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init);
    });
    pluginManager->on("ota:update:end", [this](Event const &) {
        updateActive = false;
        rerender = true;
        changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init);
    });
    pluginManager->on("ota:update:status", [this](Event const &event) {
        rerender = true;
        updateAvailable = event.getInt("value");
    });
    pluginManager->on("controller:error", [this](Event const &) {
        rerender = true;
        changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init);
    });
    pluginManager->on("controller:autotune:start",
                      [this](Event const &) { changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init); });
    pluginManager->on("controller:autotune:result",
                      [this](Event const &) { changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init); });

    pluginManager->on("profiles:profile:select", [this](Event const &event) {
        profileManager->loadSelectedProfile(selectedProfile);
        selectedProfileId = event.getString("id");
        targetDuration = profileManager->getSelectedProfile().getTotalDuration();
        targetVolume = profileManager->getSelectedProfile().getTotalVolume();
        profileVolumetric = profileManager->getSelectedProfile().isVolumetric();
        reloadProfiles();
        rerender = true;
    });
    pluginManager->on("profiles:profile:favorite", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("profiles:profile:unfavorite", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("profiles:profile:save", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("beans:selected", [this](Event const &event) {
        selectedBean = event.getString("name");
        rerender = true;
    });
    pluginManager->on("controller:volumetric-measurement:bluetooth:change", [=](Event const &event) {
        double newWeight = event.getFloat("value");
        if (round(newWeight * 10.0) != round(bluetoothWeight * 10.0)) {
            bluetoothWeight = newWeight;
            rerender = true;
        }
    });
    setupState();
    setupReactive();
    xTaskCreatePinnedToCore(loopTask, "DefaultUI::loop", configMINIMAL_STACK_SIZE * 6, this, 1, &taskHandle, 1);
    xTaskCreatePinnedToCore(profileLoopTask, "DefaultUI::loopProfiles", configMINIMAL_STACK_SIZE * 4, this, 1, &profileTaskHandle,
                            0);
}

void DefaultUI::loop() {
    const unsigned long now = millis();
    const unsigned long diff = now - lastRender;

    if (pendingAutoSteam) {
        pendingAutoSteam = false;
        // Controller::deactivate() triggers brew:end without changing mode, so
        // the mode stays MODE_BREW after a normal shot ends. The guard only
        // fires false if the user explicitly navigated away (e.g. to standby)
        // between brew:end and this loop tick, in which case discarding is correct.
        if (controller->getMode() == MODE_BREW) {
            controller->clear();
            controller->setMode(MODE_STEAM);
        }
    }

    if (now - lastTempLog > TEMP_HISTORY_INTERVAL) {
        updateTempHistory();
        lastTempLog = now;
    }

    if ((controller->isActive() && diff > RERENDER_INTERVAL_ACTIVE) || diff > RERENDER_INTERVAL_IDLE) {
        rerender = true;
    }

    if (rerender) {
        rerender = false;
        lastRender = now;
        error = controller->isErrorState();
        autotuning = controller->isAutotuning();
        const Settings &settings = controller->getSettings();
        volumetricAvailable = controller->isVolumetricAvailable();
        bluetoothScales = controller->isBluetoothScaleHealthy();
        volumetricMode = volumetricAvailable && settings.isVolumetricTarget();
        brewVolumetric = volumetricAvailable && profileVolumetric;
        grindActive = controller->isGrindActive();
        active = controller->isActive();
        smartGrindActive = settings.isSmartGrindActive();
        grindAvailable = smartGrindActive || settings.getAltRelayFunction() == ALT_RELAY_GRIND;
        applyTheme();
        applyScreenVisualLanguage();
        if (controller->isErrorState()) {
            changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init);
        }
        updateTempStableFlag();
        handleScreenChange();
        currentScreen = lv_scr_act();
        applyScreenVisualLanguage();
        if (lv_scr_act() == ui_StandbyScreen)
            updateStandbyScreen();
        if (lv_scr_act() == ui_StatusScreen)
            updateStatusScreen();
        // CAR-308 P2 #2 (Codex review): drive the status-bar clock on the
        // mode launcher too. ui_ModeScreen calls gm_status_bar(scr, false)
        // and points gm_h.status_time at its own clock label, but neither
        // updateStandbyScreen() nor updateStatusScreen() runs while
        // ModeScreen is active — so without this branch the launcher's
        // clock stayed at the gm_status_bar() builder placeholder ("--:--").
        // updateStatusBarClock() is the shared formatter used by
        // updateStatusScreen() above; it no-ops when gm_h.status_time is
        // NULL/invalid, so this is safe to call unconditionally for
        // ModeScreen.
        if (lv_scr_act() == ui_ModeScreen)
            updateStatusBarClock();
        // CAR-315: BrewScreen also owns a gm_status_bar() and points
        // gm_h.status_time at its own clock (bs_status_time), but no
        // per-frame update path drove it — so the clock stuck at the
        // builder placeholder and rendered as missing-glyph rectangles.
        // updateStatusBarClock() no-ops on NULL/invalid handles, so this is
        // safe to call unconditionally while BrewScreen is active.
        if (lv_scr_act() == ui_BrewScreen)
            updateStatusBarClock();
        // CAR-315 (PR #151 Codex review P2): GrindScreen also owns a
        // gm_status_bar(ui_GrindScreen, true) and points gm_h.status_time at
        // its own clock (gs_status_time), but had no per-frame update path.
        // Since gm_status_bar() now seeds the clock label HIDDEN (so the
        // "--:--" placeholder never renders as missing-glyph rectangles), a
        // screen with no update hook would keep its clock hidden forever.
        // updateStatusBarClock() no-ops on NULL/invalid handles, so this is
        // safe to call unconditionally while GrindScreen is active.
        if (lv_scr_act() == ui_GrindScreen)
            updateStatusBarClock();
        effect_mgr.evaluate_all();
    }

    lv_task_handler();
}

void DefaultUI::loopProfiles() {
    if (!profileLoaded) {
        favoritedProfileIds.clear();
        favoritedProfiles.clear();
        favoritedProfileIds.emplace_back(controller->getSettings().getSelectedProfile());
        for (auto &id : profileManager->getFavoritedProfiles()) {
            if (std::find(favoritedProfileIds.begin(), favoritedProfileIds.end(), id) == favoritedProfileIds.end())
                favoritedProfileIds.emplace_back(id);
        }
        for (const auto &profileId : favoritedProfileIds) {
            Profile profile{};
            profileManager->loadProfile(profileId, profile);
            favoritedProfiles.emplace_back(profile);
        }
        profileLoaded = 1;
    }
}

void DefaultUI::changeScreen(lv_obj_t **screen, void (*target_init)()) {
    targetScreen = screen;
    targetScreenInit = target_init;
    rerender = true;

    // Reset some submenus
    brewScreenState = BrewScreenState::Brew;
}

void DefaultUI::changeBrewScreenMode(BrewScreenState state) {
    brewScreenState = state;
    rerender = true;
}

void DefaultUI::onProfileSwitch() {
    currentProfileIdx = 0;
    changeScreen(&ui_ProfileScreen, ui_ProfileScreen_screen_init);
}

void DefaultUI::onNextProfile() {
    if (currentProfileIdx < favoritedProfileIds.size() - 1) {
        currentProfileIdx++;
    }
    rerender = true;
}

void DefaultUI::onPreviousProfile() {
    if (currentProfileIdx > 0) {
        currentProfileIdx--;
    }
    rerender = true;
}

void DefaultUI::onProfileSelect() {
    profileManager->selectProfile(favoritedProfileIds[currentProfileIdx]);
    profileDirty = false;
    changeScreen(&ui_BrewScreen, ui_BrewScreen_screen_init);
}

void DefaultUI::onVolumetricDelete() {
    controller->onVolumetricDelete();
    profileVolumetric = profileManager->getSelectedProfile().isVolumetric();
    profileDirty = true;
}

void DefaultUI::setupPanel() {
    ui_init();
    lv_task_handler();

    delay(100);
    // Set initial brightness based on settings
    const Settings &settings = controller->getSettings();
    setBrightness(settings.getMainBrightness());
}

void DefaultUI::setupState() {
    error = controller->isErrorState();
    autotuning = controller->isAutotuning();
    const Settings &settings = controller->getSettings();
    volumetricAvailable = controller->isVolumetricAvailable();
    volumetricMode = volumetricAvailable && settings.isVolumetricTarget();
    grindActive = controller->isGrindActive();
    active = controller->isActive();
    smartGrindActive = settings.isSmartGrindActive();
    grindAvailable = smartGrindActive || settings.getAltRelayFunction() == ALT_RELAY_GRIND;
    mode = controller->getMode();
    currentTemp = static_cast<int>(controller->getCurrentTemp());
    targetTemp = static_cast<int>(controller->getTargetTemp());
    targetDuration = profileManager->getSelectedProfile().getTotalDuration();
    targetVolume = profileManager->getSelectedProfile().getTotalVolume();
    grindDuration = settings.getTargetGrindDuration();
    grindVolume = settings.getTargetGrindVolume();
    pressureAvailable = controller->getSystemInfo().capabilities.pressure ? 1 : 0;
    pressureScaling = std::ceil(settings.getPressureScaling());
    selectedProfileId = settings.getSelectedProfile();
    profileManager->loadSelectedProfile(selectedProfile);
    profileVolumetric = selectedProfile.isVolumetric();
}

void DefaultUI::setupReactive() {
    // adjustDials() paints the dial arc colors from the live palette using plain (non-themeable) local styles. A theme
    // switch makes ui_theme_set() reapply the themeable arc-color registrations in ui_comp_dials.c, which would otherwise
    // overwrite those palette colors until the next dial recalculation. Depending on currentThemeMode here forces a full
    // restyle on theme change so the dials never snap back to the generated defaults.
    // CAR-308: ui_ModeScreen no longer hosts a SquareLine dials component —
    // the rebuilt Nothing-theme launcher is a static hub with no live
    // pressure/temp readouts. No use_effect dial reactor here.
    // CAR-278: ui_StatusScreen no longer hosts a SquareLine dials component —
    // the rebuilt screen owns its visuals via gm_h. updateStatusScreen() drives
    // accents and metric values directly, so no use_effect dial reactor here.
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; }, [=]() { adjustDials(ui_BrewScreen_dials); },
                          &pressureAvailable, &currentThemeMode);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; }, [=]() { adjustDials(ui_GrindScreen_dials); },
                          &pressureAvailable, &currentThemeMode);
    effect_mgr.use_effect([=] { return currentScreen == ui_ProfileScreen; }, [=]() { adjustDials(ui_ProfileScreen_dials); },
                          &pressureAvailable, &currentThemeMode);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; }, [=]() { adjustHeatingIndicator(ui_BrewScreen_dials); },
                          &isTemperatureStable, &heatingFlash);
    // CAR-292: ui_StatusScreen has no SquareLine dials/heating-indicator
    // widgets; visuals are driven by updateStatusScreen() + gm_status_apply_mode().
    // CAR-308: ui_ModeScreen has no SquareLine dials/heating-indicator widgets
    // anymore; the Nothing-theme launcher doesn't surface live heating state.
    effect_mgr.use_effect([=] { return currentScreen == ui_ProfileScreen; },
                          [=]() { adjustHeatingIndicator(ui_ProfileScreen_dials); }, &isTemperatureStable, &heatingFlash);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() { adjustHeatingIndicator(ui_GrindScreen_dials); }, &isTemperatureStable, &heatingFlash);
    // CAR-278: status screen no longer has a SquareLine dial widget for the
    // heating indicator. updateStatusScreen() handles its visuals directly.
    // CAR-292: ui_StatusScreen sets the kicker text per mode (BREW / STEAM /
    // WATER) inline in updateStatusScreen(); no separate effect needed.
    // CAR-308: no temp readout on the rebuilt mode launcher — live boiler temp
    // surfaces on ui_StatusScreen / ui_BrewScreen / ui_GrindScreen instead.
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=]() {
                              lv_label_set_text_fmt(uic_BrewScreen_dials_tempText, "%d°C", currentTemp);
                              // CAR-301 review C2: mirror live boiler temp into the new
                              // chat2 hero numeral. The dials cluster's tempText is no
                              // longer the prominent readout — the centered ndot_150
                              // numeral is. Hero is integer-only ("93") with the
                              // separate "°" suffix label above; no °C unit here.
                              if (uic_BrewScreen_hero_value != nullptr && lv_obj_is_valid(uic_BrewScreen_hero_value)) {
                                  lv_label_set_text_fmt(uic_BrewScreen_hero_value, "%d", currentTemp);
                              }
                          },
                          &currentTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() {
                              lv_label_set_text_fmt(uic_GrindScreen_dials_tempText, "%d°C", currentTemp);
                          },
                          &currentTemp);
    // CAR-292: current temp on ui_StatusScreen is driven directly through
    // gm_h.m_temp / gm_h.w_temp / gm_h.hero in updateStatusScreen().
    effect_mgr.use_effect([=] { return currentScreen == ui_ProfileScreen; },
                          [=]() {
                              lv_label_set_text_fmt(uic_ProfileScreen_dials_tempText, "%d°C", currentTemp);
                          },
                          &currentTemp);
    // CAR-308: no target-temp dial on the rebuilt mode launcher.
    // CAR-279 review fix: Quick-settings brew-temp readout is driven by the
    // same targetTemp signal so the +/- stepper gives immediate feedback and
    // the initial value reflects the real target instead of the static "93"
    // placeholder set in ui_MenuScreen_screen_init.
    effect_mgr.use_effect([=] { return currentScreen == ui_MenuScreen; },
                          [=]() {
                              if (lv_obj_is_valid(ui_MenuScreen_brewTempValue)) {
                                  lv_label_set_text_fmt(ui_MenuScreen_brewTempValue, "%d", targetTemp);
                              }
                          },
                          &targetTemp);
    // CAR-278: target temp surfaces on the status screen via the steam-mode
    // arc + hero readout, both updated from updateStatusScreen().
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=]() {
                              lv_label_set_text_fmt(ui_BrewScreen_targetTemp, "%d°C", targetTemp);
                              adjustTempTarget(ui_BrewScreen_dials);
                          },
                          &targetTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; }, [=]() { adjustTempTarget(ui_GrindScreen_dials); },
                          &targetTemp);
    // CAR-292: target temp on ui_StatusScreen surfaces through the steam-mode
    // arc + hero readout populated each frame in updateStatusScreen().
    effect_mgr.use_effect([=] { return currentScreen == ui_ProfileScreen; }, [=]() { adjustTempTarget(ui_ProfileScreen_dials); },
                          &targetTemp);
    // CAR-308: no pressure dial on the rebuilt mode launcher.
    // CAR-278: status screen pressure shows in the brew metric row
    // (gm_h.m_press) — populated from updateStatusScreen() each tick.
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=]() {
                              lv_arc_set_value(uic_BrewScreen_dials_pressureGauge, pressure * 10.0f);
                              lv_label_set_text_fmt(uic_BrewScreen_dials_pressureText, "%.1f bar", pressure);
                          },
                          &pressure);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() {
                              lv_arc_set_value(uic_GrindScreen_dials_pressureGauge, pressure * 10.0f);
                              lv_label_set_text_fmt(uic_GrindScreen_dials_pressureText, "%.1f bar", pressure);
                          },
                          &pressure);
    // CAR-292: pressure on ui_StatusScreen flows through gm_h.m_press in
    // updateStatusScreen() — no SimpleProcessScreen effect needed.
    effect_mgr.use_effect([=] { return currentScreen == ui_ProfileScreen; },
                          [=]() {
                              lv_arc_set_value(uic_ProfileScreen_dials_pressureGauge, pressure * 10.0f);
                              lv_label_set_text_fmt(uic_ProfileScreen_dials_pressureText, "%.1f bar", pressure);
                          },
                          &pressure);
    effect_mgr.use_effect([=] { return currentScreen == ui_StandbyScreen; },
                          [=]() {
                              updateAvailable ? lv_obj_clear_flag(ui_StandbyScreen_updateIcon, LV_OBJ_FLAG_HIDDEN)
                                              : lv_obj_add_flag(ui_StandbyScreen_updateIcon, LV_OBJ_FLAG_HIDDEN);
                          },
                          &updateAvailable);
    effect_mgr.use_effect([=] { return currentScreen == ui_StandbyScreen; },
                          [=]() {
                              // Drive the standby kicker line: a state message when not ready,
                              // otherwise the ambient "STANDBY · READY". (spacemono_14 is
                              // uppercase-only, so messages are uppercased.)
                              const char *msg = nullptr;
                              if (updateActive) {
                                  msg = "UPDATING";
                              } else if (error) {
                                  msg = controller->getError() == ERROR_CODE_RUNAWAY ? "TEMP ERROR \xC2\xB7 RESTART" : "ERROR \xC2\xB7 RESTART";
                              } else if (autotuning) {
                                  msg = "AUTOTUNING";
                              } else if (waitingForController) {
                                  msg = "WAITING FOR CONTROLLER";
                              } else if (!initialized) {
                                  msg = "STARTING";
                              }
                              lv_label_set_text(ui_StandbyScreen_mainLabel, msg ? msg : "STANDBY \xC2\xB7 READY");
                          },
                          &updateActive, &updateAvailable, &error, &autotuning, &waitingForController, &initialized);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=]() {
                              if (brewVolumetric) {
                                  lv_label_set_text_fmt(ui_BrewScreen_targetDuration, "%.1fg", targetVolume);
                              } else {
                                  const double secondsDouble = targetDuration;
                                  const auto minutes = static_cast<int>(secondsDouble / 60.0);
                                  const auto seconds = static_cast<int>(secondsDouble) % 60;
                                  lv_label_set_text_fmt(ui_BrewScreen_targetDuration, "%2d:%02d", minutes, seconds);
                              }
                          },
                          &targetDuration, &targetVolume, &brewVolumetric);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() {
                              if (volumetricMode) {
                                  lv_label_set_text_fmt(ui_GrindScreen_targetDuration, "%.1fg", grindVolume);
                              } else {
                                  const double secondsDouble = grindDuration / 1000.0;
                                  const auto minutes = static_cast<int>(secondsDouble / 60.0);
                                  const auto seconds = static_cast<int>(secondsDouble) % 60;
                                  lv_label_set_text_fmt(ui_GrindScreen_targetDuration, "%2d:%02d", minutes, seconds);
                              }
                          },
                          &grindDuration, &grindVolume, &volumetricMode);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=]() {
                              lv_img_set_src(ui_BrewScreen_Image4, brewVolumetric ? &ui_img_1424216268 : &ui_img_360122106);
                              _ui_flag_modify(ui_BrewScreen_byTimeButton, LV_OBJ_FLAG_HIDDEN, brewVolumetric);
                          },
                          &brewVolumetric);
    effect_mgr.use_effect(
        [=] { return currentScreen == ui_GrindScreen; },
        [=]() {
            lv_img_set_src(ui_GrindScreen_targetSymbol, volumetricMode ? &ui_img_1424216268 : &ui_img_360122106);
            ui_object_set_themeable_style_property(ui_GrindScreen_weightLabel, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_TEXT_COLOR,
                                                   volumetricMode ? _ui_theme_color_Dark : _ui_theme_color_NiceWhite);
            ui_object_set_themeable_style_property(ui_GrindScreen_volumetricButton, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR,
                                                   volumetricMode ? _ui_theme_color_Dark : _ui_theme_color_NiceWhite);
            ui_object_set_themeable_style_property(ui_GrindScreen_modeSwitch, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR,
                                                   volumetricMode ? _ui_theme_color_NiceWhite : _ui_theme_color_Dark);
        },
        &volumetricMode);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() { _ui_flag_modify(ui_GrindScreen_modeSwitch, LV_OBJ_FLAG_HIDDEN, volumetricAvailable); },
                          &volumetricAvailable);
    // CAR-292: SimpleProcessScreen retired. Steam/water goButton visibility
    // and graphics were the legacy affordance; ui_StatusScreen conveys
    // active state via the arc, READY pill, and progress bar instead.
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() {
                              lv_imgbtn_set_src(ui_GrindScreen_startButton, LV_IMGBTN_STATE_RELEASED, nullptr,
                                                grindActive ? &ui_img_1456692430 : &ui_img_445946954, nullptr);
                          },
                          &grindActive);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=] {
                              lv_label_set_text(ui_BrewScreen_profileName, selectedProfile.label.c_str());
                              lv_label_set_text(ui_BrewScreen_Label1,
                                                selectedBean.isEmpty() ? "PROFILE READY" : buildContextLine("Bean", selectedBean).c_str());
                              // CAR-301 review C3: drive the chat2 ratio sub-line from
                              // the selected profile. Without dose-in (bean-side, not on
                              // Profile), the line shows the achievable target shape:
                              //   volumetric → "→ {targetVolume}g · {duration}s · {maxBar} BAR"
                              //   time-based → "{duration}s · {maxBar} BAR"
                              // maxBar is the maximum pumpAdvanced.pressure across BREW
                              // phases (or 9 fallback for simple/legacy profiles).
                              if (uic_BrewScreen_ratio_sub != nullptr && lv_obj_is_valid(uic_BrewScreen_ratio_sub)) {
                                  float maxBar = 0.0f;
                                  for (const auto &phase : selectedProfile.phases) {
                                      if (phase.phase != PhaseType::PHASE_TYPE_BREW) continue;
                                      if (phase.pumpIsSimple) continue;
                                      if (phase.pumpAdvanced.target != PumpTarget::PUMP_TARGET_PRESSURE) continue;
                                      const float p = phase.pumpAdvanced.pressure;
                                      if (p > maxBar) maxBar = p;
                                  }
                                  if (maxBar <= 0.0f) maxBar = 9.0f;
                                  // CAR-301 review C5: read live targets, not selectedProfile
                                  // accessors. selectedProfile is a UI-local snapshot refreshed
                                  // only on profiles:profile:select; +/- and yield-slider edits
                                  // mutate profileManager's owned profile and fire
                                  // controller:target{Duration,Volume}:change which keep
                                  // targetDuration / targetVolume / brewVolumetric current.
                                  // Mirrors the targetDuration label effect at lines 945-963.
                                  const int totalDur = static_cast<int>(targetDuration);
                                  const float vol = targetVolume;
                                  if (brewVolumetric && vol > 0.0f) {
                                      lv_label_set_text_fmt(uic_BrewScreen_ratio_sub,
                                                            "\xE2\x86\x92 %.0fg  \xC2\xB7  %ds  \xC2\xB7  %.0f BAR",
                                                            vol, totalDur, maxBar);
                                  } else {
                                      lv_label_set_text_fmt(uic_BrewScreen_ratio_sub,
                                                            "%ds  \xC2\xB7  %.0f BAR",
                                                            totalDur, maxBar);
                                  }
                              }
                          },
                          &selectedProfileId, &selectedBean, &targetDuration, &targetVolume, &brewVolumetric);

    effect_mgr.use_effect(
        [=] { return currentScreen == ui_ProfileScreen; },
        [=] {
            if (profileLoaded) {
                _ui_flag_modify(ui_ProfileScreen_profileDetails, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
                _ui_flag_modify(ui_ProfileScreen_loadingSpinner, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
                lv_label_set_text(ui_ProfileScreen_profileName, favoritedProfiles[currentProfileIdx].label.c_str());
                lv_label_set_text(ui_ProfileScreen_mainLabel, currentProfileIdx == 0 ? "Current profile" : "Select profile");

                const auto minutes = static_cast<int>(favoritedProfiles[currentProfileIdx].getTotalDuration() / 60.0 - 0.5);
                const auto seconds = static_cast<int>(favoritedProfiles[currentProfileIdx].getTotalDuration()) % 60;
                lv_label_set_text_fmt(ui_ProfileScreen_targetDuration2, "%2d:%02d", minutes, seconds);
                lv_label_set_text_fmt(ui_ProfileScreen_targetTemp2, "%d°C",
                                      static_cast<int>(favoritedProfiles[currentProfileIdx].temperature));
                unsigned int phaseCount = favoritedProfiles[currentProfileIdx].getPhaseCount();
                unsigned int stepCount = favoritedProfiles[currentProfileIdx].phases.size();
                lv_label_set_text_fmt(ui_ProfileScreen_stepsLabel, "%d step%s", stepCount, stepCount > 1 ? "s" : "");
                lv_label_set_text_fmt(ui_ProfileScreen_phasesLabel, "%d phase%s", phaseCount, phaseCount > 1 ? "s" : "");
                ensureProfileBeanLabel();
                if (profileBeanLabel != nullptr) {
                    if (selectedBean.isEmpty()) {
                        lv_obj_add_flag(profileBeanLabel, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_clear_flag(profileBeanLabel, LV_OBJ_FLAG_HIDDEN);
                        lv_label_set_text_fmt(profileBeanLabel, "Bean • %s", selectedBean.c_str());
                    }
                }
            } else {
                _ui_flag_modify(ui_ProfileScreen_profileDetails, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
                _ui_flag_modify(ui_ProfileScreen_loadingSpinner, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
            }

            ui_object_set_themeable_style_property(ui_ProfileScreen_previousProfileBtn, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR,
                                                   currentProfileIdx > 0 ? _ui_theme_color_NiceWhite : _ui_theme_color_SemiDark);
            ui_object_set_themeable_style_property(ui_ProfileScreen_previousProfileBtn, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR_OPA,
                                                   currentProfileIdx > 0 ? _ui_theme_alpha_NiceWhite : _ui_theme_alpha_SemiDark);
            ui_object_set_themeable_style_property(
                ui_ProfileScreen_nextProfileBtn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_IMG_RECOLOR,
                currentProfileIdx < favoritedProfiles.size() - 1 ? _ui_theme_color_NiceWhite : _ui_theme_color_SemiDark);
            ui_object_set_themeable_style_property(
                ui_ProfileScreen_nextProfileBtn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_IMG_RECOLOR_OPA,
                currentProfileIdx < favoritedProfiles.size() - 1 ? _ui_theme_alpha_NiceWhite : _ui_theme_alpha_SemiDark);
        },
        &selectedProfileId, &profileLoaded, &selectedBean);

    // Show/hide grind button based on SmartGrind setting or Alt Relay function.
    // CAR-316: when no grinder is available, the bottom-right (4th) grid slot
    // shows a Settings tile and the floating top-right gear is hidden; when a
    // grinder is available, keep the Grind tile + floating gear (original
    // behavior). Driven from this same effect so it tracks the setting live.
    effect_mgr.use_effect([=] { return currentScreen == ui_ModeScreen; },
                          [=]() {
                              if (grindAvailable) {
                                  lv_obj_clear_flag(ui_ModeScreen_grindBtn, LV_OBJ_FLAG_HIDDEN);
                                  lv_obj_add_flag(ui_ModeScreen_settingsTile, LV_OBJ_FLAG_HIDDEN);
                                  lv_obj_clear_flag(ui_ModeScreen_settingsButton, LV_OBJ_FLAG_HIDDEN);
                              } else {
                                  lv_obj_add_flag(ui_ModeScreen_grindBtn, LV_OBJ_FLAG_HIDDEN);
                                  lv_obj_clear_flag(ui_ModeScreen_settingsTile, LV_OBJ_FLAG_HIDDEN);
                                  lv_obj_add_flag(ui_ModeScreen_settingsButton, LV_OBJ_FLAG_HIDDEN);
                              }
                          },
                          &grindAvailable);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=]() {
                              if (volumetricAvailable && bluetoothScales) {
                                  lv_label_set_text_fmt(ui_BrewScreen_weightLabel, "%.1fg", bluetoothWeight);
                              } else {
                                  lv_label_set_text(ui_BrewScreen_weightLabel, "-");
                              }
                          },
                          &bluetoothWeight, &volumetricAvailable, &bluetoothScales);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() {
                              if (volumetricAvailable && bluetoothScales) {
                                  lv_label_set_text_fmt(ui_GrindScreen_weightLabel, "%.1fg", bluetoothWeight);
                              } else {
                                  lv_label_set_text(ui_GrindScreen_weightLabel, "-");
                              }
                              ensureGrindBeanLabel();
                              if (grindBeanLabel != nullptr) {
                                  if (selectedBean.isEmpty()) {
                                      lv_obj_add_flag(grindBeanLabel, LV_OBJ_FLAG_HIDDEN);
                                  } else {
                                      lv_obj_clear_flag(grindBeanLabel, LV_OBJ_FLAG_HIDDEN);
                                      lv_label_set_text_fmt(grindBeanLabel, "Bean • %s", selectedBean.c_str());
                                  }
                              }
                          },
                          &bluetoothWeight, &volumetricAvailable, &bluetoothScales, &selectedBean);
    effect_mgr.use_effect(
        [=] { return currentScreen == ui_BrewScreen; },
        [=]() {
            const bool settings = brewScreenState == BrewScreenState::Settings;
            // CAR-315: the CAR-301 chat2 hero numeral / status pill / ratio sub
            // were never toggled with the sub-state, so in the Settings (+/-
            // editor) sub-state they bled through behind the steppers, and in
            // the landing (Brew) sub-state they stacked on top of profileInfo,
            // modeSwitch and START SHOT. Treat the two sub-states as distinct
            // coherent layouts (strategy A): show the hero readout only in the
            // landing state, hide it while editing, and re-anchor the landing
            // widgets so nothing overlaps the 135px-tall hero numeral.
            if (uic_BrewScreen_hero_value != nullptr && lv_obj_is_valid(uic_BrewScreen_hero_value))
                _ui_flag_modify(uic_BrewScreen_hero_value, LV_OBJ_FLAG_HIDDEN, !settings);
            if (uic_BrewScreen_hero_unit != nullptr && lv_obj_is_valid(uic_BrewScreen_hero_unit))
                _ui_flag_modify(uic_BrewScreen_hero_unit, LV_OBJ_FLAG_HIDDEN, !settings);
            if (uic_BrewScreen_status_pill != nullptr && lv_obj_is_valid(uic_BrewScreen_status_pill))
                _ui_flag_modify(uic_BrewScreen_status_pill, LV_OBJ_FLAG_HIDDEN, !settings);
            // ratio sub-line only adds value once a profile/target is in view;
            // keep it hidden in both sub-states to avoid crowding the hero in
            // landing and the steppers in Settings (CAR-315). The status pill
            // and dials ring already convey live state.
            //
            // NOTE (PR #151 review M2): this is the DELIBERATE end state for the
            // CAR-301 ratio_sub widget — it is intentionally dead UI in every
            // BrewScreen sub-state, not a bug. The widget + its handle are kept
            // so a future ticket can re-surface a ratio breakdown ("18 -> 36 ·
            // 1:2 · 9 BAR") without re-adding the layout scaffolding. Do not
            // "restore" its visibility without a design decision behind it.
            if (uic_BrewScreen_ratio_sub != nullptr && lv_obj_is_valid(uic_BrewScreen_ratio_sub))
                lv_obj_add_flag(uic_BrewScreen_ratio_sub, LV_OBJ_FLAG_HIDDEN);

            _ui_flag_modify(ui_BrewScreen_adjustments, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Settings);
            _ui_flag_modify(ui_BrewScreen_acceptButton, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Settings);
            _ui_flag_modify(ui_BrewScreen_saveButton, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Settings);
            _ui_flag_modify(ui_BrewScreen_saveAsNewButton, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Settings);
            _ui_flag_modify(ui_BrewScreen_startButton, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Brew);
            _ui_flag_modify(ui_BrewScreen_profileInfo, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Brew);
            // PR #153 review (Codex P1 #3353255295 / P2 #3355129176): the weight/
            // mode chip is NOT part of the round brew-landing design (chat2
            // BrewReady: hero → pill → ratio → START SHOT → chip bar, no weight
            // chip). The old landing reparented it to center+60 where its 180x50
            // body overlapped the START SHOT button / HEATING pill at BOTTOM_MID
            // -104 and intercepted their taps. On round, show it ONLY in Settings
            // AND only when volumetric is available (predicate true => REMOVE
            // HIDDEN => shown); the Brew/Profile landing sub-states hide it.
            // PR #153 review (Codex P2 #3356478651): the volumetricAvailable guard
            // is required here too — without it, Settings on a scaleless round
            // machine shows a nonfunctional "-" chip that is still long-pressable
            // and fires onVolumetricHold() tare commands. Rectangular keeps the
            // original Brew+volumetric gate.
            _ui_flag_modify(ui_BrewScreen_modeSwitch, LV_OBJ_FLAG_HIDDEN,
                            isRoundDisplay()
                                ? (brewScreenState == BrewScreenState::Settings && volumetricAvailable)
                                : (brewScreenState == BrewScreenState::Brew && volumetricAvailable));
            if (volumetricAvailable) {
                lv_img_set_src(ui_BrewScreen_volumetricButton, bluetoothScales ? &ui_img_1424216268 : &ui_img_flowmeter_png);
            }

            // CAR-315: round-display landing-state layout. Stack everything in
            // the margins above/below the centered hero numeral so the profile
            // selector, status pill, weight chip and START SHOT never collide.
            // Panel is the 372×372 round circle (CENTER-relative coords). The
            // rectangular path keeps the original SquareLine positions (AC #8).
            if (isRoundDisplay() && !settings) {
                // CAR-318: while the boiler is still heating, the live status
                // pill ("HEATING") sits at the BOTTOM (clear of the profile
                // selector) and START SHOT is hidden — you can't pull a shot
                // until the machine is at temperature. Once isTemperatureStable
                // flips true, hide the pill and reveal START SHOT in the same
                // bottom slot. (isTemperatureStable resets to false on any
                // setpoint change, so the pill returns if the target moves.)
                // _ui_flag_modify is tri-state: arg 0 ADDs the flag, 1 REMOVEs
                // it (ui_helpers.c:80-89), so a bool reads as "true => REMOVE
                // (show)". To SHOW the pill while heating, pass !atTarget.
                const bool atTarget = isTemperatureStable;
                if (uic_BrewScreen_status_pill != nullptr && lv_obj_is_valid(uic_BrewScreen_status_pill)) {
                    // CAR-319: lifted above the bottom mode-chip pill bar (which
                    // sits at BOTTOM_MID y=-34, ~56px tall). -104 clears it.
                    lv_obj_align(uic_BrewScreen_status_pill, LV_ALIGN_BOTTOM_MID, 0, -104);
                    _ui_flag_modify(uic_BrewScreen_status_pill, LV_OBJ_FLAG_HIDDEN, !atTarget);
                }
                // Single-row profile selector — hide the tall "Selected profile"
                // caption and bean-context label so the pill stays compact and
                // clears the hero numeral.
                if (lv_obj_is_valid(ui_BrewScreen_Label1))
                    lv_obj_add_flag(ui_BrewScreen_Label1, LV_OBJ_FLAG_HIDDEN);
                if (brewContextLabel != nullptr && lv_obj_is_valid(brewContextLabel))
                    lv_obj_add_flag(brewContextLabel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_height(ui_BrewScreen_profileInfo, 56);
                lv_obj_align(ui_BrewScreen_profileInfo, LV_ALIGN_TOP_MID, 0, 50);
                // Hero temperature numeral, centered.
                if (uic_BrewScreen_hero_value != nullptr && lv_obj_is_valid(uic_BrewScreen_hero_value)) {
                    lv_obj_align(uic_BrewScreen_hero_value, LV_ALIGN_CENTER, -16, -14);
                    if (uic_BrewScreen_hero_unit != nullptr && lv_obj_is_valid(uic_BrewScreen_hero_unit))
                        lv_obj_align_to(uic_BrewScreen_hero_unit, uic_BrewScreen_hero_value,
                                        LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -8);
                }
                // PR #153 review: the weight/mode chip is hidden on the round
                // landing (see the modeSwitch gate above), so there is nothing to
                // reparent/position here anymore — the design's middle stack is
                // hero → pill → ratio → START SHOT, with no weight chip. The
                // Settings branch reparents modeSwitch back to controlContainer.
                lv_obj_align(ui_BrewScreen_startButton, LV_ALIGN_BOTTOM_MID, 0, -104);
                // CAR-318 (PR #153 review 4424398936 P1/P2): the START SHOT
                // button and the HEATING status pill swap mutually-exclusively on
                // atTarget at the same BOTTOM_MID slot, so they never overlap.
                // Button is HIDDEN while heating (atTarget==0 => ADD HIDDEN) and
                // shown once at-target (atTarget==1 => REMOVE HIDDEN); the pill
                // above does the inverse (!atTarget). Hiding the button while
                // heating means it can no longer paint over the pill or sit as an
                // enabled-looking control whose click silently no-ops.
                _ui_flag_modify(ui_BrewScreen_startButton, LV_OBJ_FLAG_HIDDEN, atTarget);
            } else if (isRoundDisplay() && settings) {
                // CAR-315 (PR #151 review I1/I2/M1): the landing block above
                // performs one-way geometry mutations (profileInfo height/anchor,
                // modeSwitch reparent, Label1/brewContextLabel hidden). Brew
                // <-> Settings toggles do NOT rebuild the screen (changeBrewScreenMode
                // only flips the signal + rerender), so without a symmetric reset
                // the Settings editor inherits the compacted landing geometry.
                // Restore the screen_init defaults so the editor sub-state renders
                // as designed.
                //
                // profileInfo: screen_init sets 292x100 @ TOP_MID y=80 specifically
                // so the chip lands correctly when un-hidden in Settings
                // (ui_BrewScreen.c:399-405, CAR-301 review C1).
                lv_obj_set_height(ui_BrewScreen_profileInfo, 100);
                lv_obj_align(ui_BrewScreen_profileInfo, LV_ALIGN_TOP_MID, 0, 80);
                // Re-show the "Selected profile" caption + bean-context label that
                // the landing block compacts away (M1).
                if (lv_obj_is_valid(ui_BrewScreen_Label1))
                    lv_obj_clear_flag(ui_BrewScreen_Label1, LV_OBJ_FLAG_HIDDEN);
                if (brewContextLabel != nullptr && lv_obj_is_valid(brewContextLabel))
                    lv_obj_clear_flag(brewContextLabel, LV_OBJ_FLAG_HIDDEN);
                // modeSwitch: reparent back to its controlContainer flex home so it
                // flows in the editor's control column (I2). Restore the original
                // child order (modeSwitch first, then adjustments) — reparenting
                // appends as the last child, so move it back to index 0.
                if (lv_obj_is_valid(ui_BrewScreen_modeSwitch) &&
                    lv_obj_get_parent(ui_BrewScreen_modeSwitch) != ui_BrewScreen_controlContainer) {
                    lv_obj_set_parent(ui_BrewScreen_modeSwitch, ui_BrewScreen_controlContainer);
                    lv_obj_move_to_index(ui_BrewScreen_modeSwitch, 0);
                }
            }
        },
        &brewScreenState, &volumetricAvailable, &bluetoothScales, &isTemperatureStable);
    effect_mgr.use_effect(
        [=] { return currentScreen == ui_BrewScreen; },
        [=]() {
            ui_object_set_themeable_style_property(ui_BrewScreen_saveButton, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR,
                                                   profileDirty ? _ui_theme_color_NiceWhite : _ui_theme_color_SemiDark);
            ui_object_set_themeable_style_property(ui_BrewScreen_saveButton, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR_OPA,
                                                   profileDirty ? _ui_theme_alpha_NiceWhite : _ui_theme_alpha_SemiDark);
            ui_object_set_themeable_style_property(ui_BrewScreen_saveAsNewButton, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR,
                                                   profileDirty ? _ui_theme_color_NiceWhite : _ui_theme_color_SemiDark);
            ui_object_set_themeable_style_property(ui_BrewScreen_saveAsNewButton, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR_OPA,
                                                   profileDirty ? _ui_theme_alpha_NiceWhite : _ui_theme_alpha_SemiDark);
        },
        &brewScreenState, &profileDirty);
    effect_mgr.use_effect([=] { return currentScreen == ui_StandbyScreen; },
                          [=]() { lv_img_set_src(ui_StandbyScreen_logo, christmasMode ? &ui_img_1510335 : &ui_img_logo_png); },
                          &christmasMode);
}

void DefaultUI::handleScreenChange() {
    // While bean overlay is open, suppress only redundant no-op checks.
    // A real transition (targetScreen changed) must go through — close the overlay first.
    if (beanSelectActive) {
        if (lv_scr_act() == *targetScreen)
            return;
        // A screen change was requested while overlay was open — close it cleanly.
        beanSelectActive = false;
        if (beanSelectScreen != nullptr) {
            lv_obj_del(beanSelectScreen);
            beanSelectScreen = nullptr;
        }
    }
    lv_obj_t *current = lv_scr_act();

    if (current != *targetScreen) {
        if (*targetScreen == ui_StandbyScreen) {
            standbyEnterTime = millis();
        } else if (current == ui_StandbyScreen) {
            const Settings &settings = controller->getSettings();
            setBrightness(settings.getMainBrightness());
        }

        _ui_screen_change(targetScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, targetScreenInit);
        resetCustomScreenHandles();
        lv_obj_del(current);
        rerender = true;
    }
}

void DefaultUI::resetCustomScreenHandles() {
    statusBeanLabel = nullptr;
    profileBeanLabel = nullptr;
    grindBeanLabel = nullptr;
    brewContextLabel = nullptr;
    // CAR-319: the chip bar lives on ui_BrewScreen and is destroyed with it on
    // screen change; null the handles so ensureBrewModeChips() rebuilds on re-entry.
    brewModeChips = nullptr;
    brewModeChip[0] = brewModeChip[1] = brewModeChip[2] = brewModeChip[3] = nullptr;
}

bool DefaultUI::isRoundDisplay() const {
    lv_disp_t *display = lv_disp_get_default();
    if (display == nullptr) {
        return false;
    }
    const lv_coord_t width = lv_disp_get_hor_res(display);
    const lv_coord_t height = lv_disp_get_ver_res(display);
    return width == height && width >= 400;
}

void DefaultUI::ensureStatusBeanLabel() {
    // CAR-278: status screen rebuilt — bean label is anchored to the screen
    // root (ui_StatusScreen). Visibility/positioning is owned by
    // updateStatusScreen() so the label tracks the current mode (brew/steam/water).
    if (lv_scr_act() != ui_StatusScreen || !lv_obj_is_valid(ui_StatusScreen) || statusBeanLabel != nullptr) {
        return;
    }

    statusBeanLabel = lv_label_create(ui_StatusScreen);
    lv_obj_set_width(statusBeanLabel, 240);
    lv_obj_align(statusBeanLabel, LV_ALIGN_TOP_MID, 0, 92);
    lv_label_set_long_mode(statusBeanLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(statusBeanLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(statusBeanLabel, LV_OBJ_FLAG_HIDDEN);

    // CAR-278 review-comment 4585899391, finding #5: chip-style theming is
    // static (GM_* tokens never change at runtime), so apply it once here
    // instead of every applyScreenVisualLanguage() pass. The label is a
    // Nothing-theme chip styled with GM_* tokens, not the legacy
    // DisplayPalette — the rest of the screen uses gm_theme tokens and
    // styleChip() would clash.
    lv_obj_set_style_bg_color(statusBeanLabel, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(statusBeanLabel, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(statusBeanLabel, GM_CONTENT, 0);
    lv_obj_set_style_text_font(statusBeanLabel, &spacemono_14, 0);
    lv_obj_set_style_radius(statusBeanLabel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(statusBeanLabel, 12, 0);
    lv_obj_set_style_pad_right(statusBeanLabel, 12, 0);
    lv_obj_set_style_pad_top(statusBeanLabel, 4, 0);
    lv_obj_set_style_pad_bottom(statusBeanLabel, 4, 0);
}

void DefaultUI::ensureProfileBeanLabel() {
    if (lv_scr_act() != ui_ProfileScreen || !lv_obj_is_valid(ui_ProfileScreen_contentPanel) || profileBeanLabel != nullptr) {
        return;
    }

    profileBeanLabel = lv_label_create(ui_ProfileScreen_contentPanel);
    lv_obj_set_width(profileBeanLabel, 240);
    lv_obj_align_to(profileBeanLabel, ui_ProfileScreen_profileName, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_label_set_long_mode(profileBeanLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(profileBeanLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void DefaultUI::ensureGrindBeanLabel() {
    if (lv_scr_act() != ui_GrindScreen || !lv_obj_is_valid(ui_GrindScreen_contentPanel7) || grindBeanLabel != nullptr) {
        return;
    }

    grindBeanLabel = lv_label_create(ui_GrindScreen_contentPanel7);
    lv_obj_set_width(grindBeanLabel, 220);
    lv_obj_align_to(grindBeanLabel, ui_GrindScreen_mainLabel7, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_label_set_long_mode(grindBeanLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(grindBeanLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// CAR-308: ensureMenuActionLabels() removed. The rebuilt ui_ModeScreen
// creates its tile labels inline in ui_ModeScreen_screen_init (via the
// build_mode_tile() helper) instead of having DefaultUI lazily attach
// them on every applyScreenPalette() pass.

void DefaultUI::ensureBrewContextLabel() {
    if (lv_scr_act() != ui_BrewScreen || !lv_obj_is_valid(ui_BrewScreen_profileInfo) || brewContextLabel != nullptr) {
        return;
    }

    brewContextLabel = lv_label_create(ui_BrewScreen_profileInfo);
    lv_obj_set_width(brewContextLabel, 240);
    lv_label_set_long_mode(brewContextLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(brewContextLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// CAR-319: bottom mode-chip pill bar on the round BrewScreen landing view,
// matching the Claude Design handoff (chat2 "Brew Screen Heating State" +
// Build Spec mode-chip bar). Four round 44px chips — power/cup/steam/drop — in
// a rounded translucent container pinned BOTTOM_MID y=-34. Cup (index 1) is the
// active brew chip (filled GM_RED). Tapping a chip switches machine mode via the
// same handlers the ModeScreen launcher uses. Built once, lazily, idempotent.
void DefaultUI::brewModeChipCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    // The chip index is stashed in the event user-data at build time.
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch (idx) {
    case 0:
        onStandby(e); // power → standby
        break;
    case 1:
        break; // cup → already on the brew screen, no-op
    case 2:
        onSteamScreen(e); // steam mode
        break;
    case 3:
        onWaterScreen(e); // water mode
        break;
    default:
        break;
    }
}

void DefaultUI::ensureBrewModeChips() {
    if (lv_scr_act() != ui_BrewScreen || !lv_obj_is_valid(ui_BrewScreen) || brewModeChips != nullptr) {
        return;
    }

    // Rounded translucent pill container, parented to the screen root so it
    // floats above contentPanel4. gm-chips: bottom 34, padding 6, gap 6.
    brewModeChips = lv_obj_create(ui_BrewScreen);
    lv_obj_remove_style_all(brewModeChips);
    lv_obj_set_height(brewModeChips, LV_SIZE_CONTENT);
    lv_obj_set_width(brewModeChips, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brewModeChips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brewModeChips, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(brewModeChips, 6, 0);
    lv_obj_set_style_pad_gap(brewModeChips, 6, 0);
    lv_obj_set_style_radius(brewModeChips, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(brewModeChips, GM_CONTENT, 0);
    lv_obj_set_style_bg_opa(brewModeChips, LV_OPA_10, 0); // ~rgba(255,255,255,0.03-0.10)
    lv_obj_set_style_border_width(brewModeChips, 1, 0);
    lv_obj_set_style_border_color(brewModeChips, GM_FAINT, 0);
    lv_obj_set_style_border_opa(brewModeChips, LV_OPA_60, 0);
    lv_obj_clear_flag(brewModeChips, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(brewModeChips, LV_ALIGN_BOTTOM_MID, 0, -34);

    const lv_img_dsc_t *icons[4] = {&gm_ic_power, &gm_ic_cup, &gm_ic_steam, &gm_ic_drop};
    const int activeChip = 1; // cup / brew
    for (int i = 0; i < 4; i++) {
        lv_obj_t *chip = lv_obj_create(brewModeChips);
        lv_obj_remove_style_all(chip);
        lv_obj_set_size(chip, 44, 44);
        lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        if (i == activeChip) {
            lv_obj_set_style_bg_color(chip, GM_RED, 0);
            lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, 0);
        }
        lv_obj_t *ic = lv_img_create(chip);
        lv_img_set_src(ic, icons[i]);
        // Match the proven ui_ModeScreen build_mode_tile() recipe: recolor the
        // (alpha) icon, force REAL size mode so the self-size equals the bitmap,
        // THEN center. PR #153 review symptom: inactive chips only showed a dot —
        // the icons were recolored GM_MUTED (0x8A8A8A) at full opacity which read
        // as near-invisible against the transparent chip, and the missing REAL
        // size mode let the centered icon collapse. Fix: active chip = near-black
        // icon on solid GM_RED; inactive chips = bright GM_CONTENT icon dimmed via
        // recolor opacity so the icon is clearly shaped/visible yet the selected
        // (red) chip still stands out.
        const bool chipActive = (i == activeChip);
        lv_obj_set_style_img_recolor(ic, chipActive ? GM_BG : GM_CONTENT, 0);
        lv_obj_set_style_img_recolor_opa(ic, chipActive ? LV_OPA_COVER : LV_OPA_70, 0);
        lv_img_set_size_mode(ic, LV_IMG_SIZE_MODE_REAL);
        lv_obj_center(ic);
        lv_obj_add_event_cb(chip, brewModeChipCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        brewModeChip[i] = chip;
    }
}

void DefaultUI::applyScreenVisualLanguage() {
    const bool amoledPanel = AmoledDisplayDriver::getInstance() == panelDriver;
    const int resolvedThemeMode = resolveDisplayThemeMode(controller->getSettings().getThemeMode(), amoledPanel);
    const DisplayPalette palette = makeDisplayPalette(resolvedThemeMode, amoledPanel);
    lv_obj_t *activeScreen = lv_scr_act();
    const bool roundDisplay = isRoundDisplay();
    const RingVisualContext ringContext{mode, currentTemp, targetTemp, active != 0, grindActive != 0, controller,
                                        isTemperatureStable != 0};
    const RingVisual ringVisual = buildRingVisual(palette, ringContext);

    styleScreenBase(activeScreen, palette, roundDisplay);

    if (activeScreen == ui_BrewScreen) {
        // CAR-293: BrewScreen self-styles via gm_ui builders + gm_theme tokens.
        // We only need to: (a) drive the ring overlay, (b) update the kicker text
        // to match ringVisual, (c) refresh the bean context label, and (d) hand
        // the palette to the screen so it can recolor on UI_THEME_LIGHT.
        ensureBrewContextLabel();
        applyProcessRing(uic_BrewScreen_dials_tempGauge, palette, ringVisual, roundDisplay);
        ui_BrewScreen_apply_palette(palette.textPrimary, palette.textMuted,
                                    palette.surface, palette.accent, ringVisual.tone);
        if (lv_obj_is_valid(ui_BrewScreen_mainLabel3)) {
            lv_label_set_text(ui_BrewScreen_mainLabel3, ringVisual.title);
            lv_obj_set_style_text_color(ui_BrewScreen_mainLabel3, ringVisual.tone, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(ui_BrewScreen_mainLabel3, &ndot_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (brewContextLabel != nullptr && lv_obj_is_valid(brewContextLabel)) {
            const String brewContext = selectedBean.isEmpty() ? "Ready to brew" : String("Bean | ") + selectedBean;
            lv_label_set_text(brewContextLabel, brewContext.c_str());
            styleSecondary(brewContextLabel, palette);
        }
        if (roundDisplay) {
            // Round-display content panel is the round 372x372 circle; the screen
            // sets a 360x360 default for flat displays, so bump it here.
            lv_obj_set_size(ui_BrewScreen_contentPanel4, 372, 372);
            lv_obj_align(ui_BrewScreen_contentPanel4, LV_ALIGN_CENTER, 0, 4);
            // CAR-319: the design replaces the top-left back/menu button with the
            // bottom mode-chip pill bar (power chip = standby). Hide the back
            // button on round; navigation now lives in the chip bar. (Kept alive,
            // not deleted, so its event/ext-click-area references stay valid.)
            if (lv_obj_is_valid(ui_BrewScreen_ImgButton5)) {
                lv_obj_add_flag(ui_BrewScreen_ImgButton5, LV_OBJ_FLAG_HIDDEN);
            }
            ensureBrewModeChips();
            // CAR-319 (Codex P1): the mode-chip pill bar is a screen-root child
            // anchored BOTTOM_MID — the same area as the Settings footer
            // (saveButton/acceptButton/saveAsNewButton). In the Settings
            // sub-state it would render above those controls and its clickable
            // chips would intercept their touches, so the user couldn't save,
            // accept, or cancel a profile edit. The footer buttons are hidden
            // outside the landing state (lines ~1190-1193); mirror that here so
            // the chip bar only shows on the Brew landing sub-state.
            // PR #153 review 4424457425 P1: predicate must be inverted. atTarget-
            // style tri-state: cond==1 REMOVEs HIDDEN (shows). Pass
            // (state != Settings) so: Settings => false => ADD HIDDEN (hidden);
            // Brew landing => true => REMOVE HIDDEN (shown).
            if (brewModeChips != nullptr && lv_obj_is_valid(brewModeChips)) {
                _ui_flag_modify(brewModeChips, LV_OBJ_FLAG_HIDDEN,
                                brewScreenState != BrewScreenState::Settings);
            }
            // brewContextLabel is parented to ui_BrewScreen_profileInfo by
            // ensureBrewContextLabel(); align it relative to Container3 on round.
            if (brewContextLabel != nullptr && lv_obj_is_valid(brewContextLabel)) {
                lv_obj_set_width(brewContextLabel, 238);
                lv_obj_align_to(brewContextLabel, ui_BrewScreen_Container3, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
            }
        }
    } else if (activeScreen == ui_StatusScreen) {
        // CAR-278: status screen styles itself via gm_ui builders + gm_theme tokens.
        // The ensureStatusBeanLabel() call here is intentionally a no-op trigger:
        // bean label visibility/text is owned by updateStatusScreen() per mode.
        // Per review-comment 4585899391 finding #5, the chip styling is applied
        // once inside ensureStatusBeanLabel() (GM_* tokens are static), so
        // there is no per-call style work to do here.
        ensureStatusBeanLabel();
    } else if (activeScreen == ui_ProfileScreen) {
        stylePanel(ui_ProfileScreen_contentPanel, palette, OPA_55, 44);
        styleHeadline(ui_ProfileScreen_mainLabel, palette, false);
        styleHeadline(ui_ProfileScreen_profileName, palette, true);
        styleSecondary(ui_ProfileScreen_phasesLabel, palette);
        styleSecondary(ui_ProfileScreen_stepsLabel, palette);
        styleIconButton(ui_ProfileScreen_ImgButton1, palette, palette.textPrimary);
        styleIconButton(ui_ProfileScreen_previousProfileBtn, palette, palette.textMuted);
        styleIconButton(ui_ProfileScreen_nextProfileBtn, palette, palette.accent);
        styleIconButton(ui_ProfileScreen_chooseButton, palette, palette.success);
        applyProcessRing(uic_ProfileScreen_dials_tempGauge, palette, ringVisual, roundDisplay);
        styleMetricValue(ui_ProfileScreen_profileName, palette, &ndot_24);
        styleMetricValue(ui_ProfileScreen_targetTemp2, palette, &ndot_24);
        styleMetricValue(ui_ProfileScreen_targetDuration2, palette, &ndot_24);
        if (lv_obj_is_valid(ui_ProfileScreen_Chart1)) {
            stylePanel(ui_ProfileScreen_Chart1, palette, OPA_190, 20);
        }
        ensureProfileBeanLabel();
        if (profileBeanLabel != nullptr && lv_obj_is_valid(profileBeanLabel)) {
            styleChip(profileBeanLabel, palette, palette.accent, true);
        }
        if (lv_obj_is_valid(ui_ProfileScreen_mainLabel)) {
            lv_label_set_text(ui_ProfileScreen_mainLabel, "Profile Preview");
            lv_obj_set_style_text_font(ui_ProfileScreen_mainLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (roundDisplay) {
            lv_obj_set_size(ui_ProfileScreen_contentPanel, 372, 372);
            lv_obj_align(ui_ProfileScreen_contentPanel, LV_ALIGN_CENTER, 0, 4);
            alignTopBackButton(ui_ProfileScreen_ImgButton1, palette, palette.textPrimary);
            lv_obj_set_size(ui_ProfileScreen_profileDetails, 318, 300);
            lv_obj_align(ui_ProfileScreen_profileDetails, LV_ALIGN_CENTER, 0, 8);
            styleFixedLabel(ui_ProfileScreen_mainLabel, 244, 24, &lv_font_montserrat_18, palette.textMuted);
            lv_obj_align(ui_ProfileScreen_mainLabel, LV_ALIGN_TOP_MID, 0, 20);
            styleFixedLabel(ui_ProfileScreen_profileName, 244, 34, &ndot_24, palette.textPrimary);
            lv_obj_align(ui_ProfileScreen_profileName, LV_ALIGN_TOP_MID, 0, 54);
            if (profileBeanLabel != nullptr) {
                lv_obj_set_size(profileBeanLabel, 232, 34);
                lv_obj_align_to(profileBeanLabel, ui_ProfileScreen_profileName, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
            }
            alignMetricPair(ui_ProfileScreen_tempIcon, ui_ProfileScreen_targetTemp2, -58, -42, palette.warning, palette);
            alignMetricPair(ui_ProfileScreen_targetIcon, ui_ProfileScreen_targetDuration2, 64, -42, palette.accent, palette);
            lv_obj_set_size(ui_ProfileScreen_simpleContent, 232, 92);
            lv_obj_align(ui_ProfileScreen_simpleContent, LV_ALIGN_CENTER, 0, 56);
            lv_obj_set_size(ui_ProfileScreen_extendedContent, 250, 116);
            lv_obj_align(ui_ProfileScreen_extendedContent, LV_ALIGN_CENTER, 0, 56);
            lv_obj_set_size(ui_ProfileScreen_Chart1, 232, 104);
            lv_obj_align(ui_ProfileScreen_Chart1, LV_ALIGN_CENTER, 0, 0);
            lv_obj_align(ui_ProfileScreen_previousProfileBtn, LV_ALIGN_LEFT_MID, 20, 6);
            lv_obj_align(ui_ProfileScreen_nextProfileBtn, LV_ALIGN_RIGHT_MID, -20, 6);
            styleRoundIconButton(ui_ProfileScreen_previousProfileBtn, palette, palette.textMuted, 56);
            styleRoundIconButton(ui_ProfileScreen_nextProfileBtn, palette, palette.accent, 56);
            alignFooterAction(ui_ProfileScreen_chooseButton, palette, palette.success);
        }
    } else if (activeScreen == ui_GrindScreen) {
        // CAR-294: GrindScreen self-styles via gm_ui builders + gm_theme tokens.
        // We only need to: (a) drive the ring overlay, (b) update the kicker
        // text/tone (GRIND vs GRINDING with palette.grind), (c) refresh the
        // bean chip, and (d) hand the palette to the screen so it can recolor
        // on UI_THEME_LIGHT.
        ensureGrindBeanLabel();
        applyProcessRing(uic_GrindScreen_dials_tempGauge, palette, ringVisual, roundDisplay);
        ui_GrindScreen_apply_palette(palette.textPrimary, palette.textMuted,
                                     palette.surface, palette.accent, palette.grind);
        if (grindBeanLabel != nullptr && lv_obj_is_valid(grindBeanLabel)) {
            styleChip(grindBeanLabel, palette, palette.accent, true);
        }
        if (lv_obj_is_valid(ui_GrindScreen_mainLabel7)) {
            lv_label_set_text(ui_GrindScreen_mainLabel7, grindActive ? "GRINDING" : "GRIND");
            lv_obj_set_style_text_font(ui_GrindScreen_mainLabel7, &ndot_24, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(ui_GrindScreen_mainLabel7, palette.grind, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (roundDisplay) {
            // Round-display layout overrides — the screen ships a 360x360 flat
            // default; bump to the 372 round circle and re-anchor children.
            lv_obj_set_size(ui_GrindScreen_contentPanel7, 372, 372);
            lv_obj_align(ui_GrindScreen_contentPanel7, LV_ALIGN_CENTER, 0, 4);
            alignTopBackButton(ui_GrindScreen_ImgButton2, palette, palette.textPrimary);
            styleFixedLabel(ui_GrindScreen_mainLabel7, 220, 32, &ndot_24, palette.grind);
            lv_obj_align(ui_GrindScreen_mainLabel7, LV_ALIGN_TOP_MID, 0, 42);
            if (grindBeanLabel != nullptr) {
                lv_obj_set_size(grindBeanLabel, 226, 34);
                lv_obj_align_to(grindBeanLabel, ui_GrindScreen_mainLabel7, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
            }
            lv_obj_set_size(ui_GrindScreen_modeSwitch, 176, 52);
            lv_obj_align(ui_GrindScreen_modeSwitch, LV_ALIGN_TOP_MID, 0, 104);
            lv_obj_set_size(ui_GrindScreen_weightLabel, 104, 28);
            lv_obj_set_style_text_font(ui_GrindScreen_weightLabel, &ndot_24, LV_PART_MAIN | LV_STATE_DEFAULT);
            // Round-display target container: drop down/up buttons to the
            // legacy left/right anchors used pre-CAR-294 so the round layout
            // keeps the wider feel. Symbol stays paired with the duration.
            lv_obj_set_size(ui_GrindScreen_targetContainer, 254, 54);
            lv_obj_align(ui_GrindScreen_targetContainer, LV_ALIGN_CENTER, 0, 32);
            lv_obj_set_style_pad_all(ui_GrindScreen_targetContainer, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
            alignMetricPair(ui_GrindScreen_targetSymbol, ui_GrindScreen_targetDuration, -10, 0, palette.accent, palette);
            // Round-display: keep the gs_glyph_btn() flat-circle styling
            // (filled GM_SURFACE / GM_BLUE, no border, no shadow) — do NOT
            // call styleRoundIconButton() here. The legacy lv_imgbtn versions
            // of these buttons used PNG glyphs that styleRoundIconButton()
            // recolored via lv_obj_set_style_img_recolor(); the new lv_btn
            // containers carry an inner lv_label glyph instead, so those
            // recolor calls are no-ops AND styleGlassButton (called inside
            // styleRoundIconButton) overrides the clean Nothing-theme
            // styling with a translucent gradient + border that erases the
            // up button's filled accent identity. Resize directly to the
            // round-display 48px so the layout still feels wide.
            // Mirrors BrewScreen's round-display branch (DefaultUI.cpp:1291-1303).
            lv_obj_set_size(ui_GrindScreen_downDurationButton, 48, 48);
            lv_obj_set_size(ui_GrindScreen_upDurationButton, 48, 48);
            lv_obj_align(ui_GrindScreen_downDurationButton, LV_ALIGN_CENTER, -104, 0);
            lv_obj_align(ui_GrindScreen_upDurationButton, LV_ALIGN_CENTER, 104, 0);
            alignFooterAction(ui_GrindScreen_startButton, palette, palette.accent);
        }
    } else if (activeScreen == ui_ModeScreen) {
        // CAR-308: Nothing-theme mode launcher styles itself via gm_* builders
        // in ui_ModeScreen_screen_init; tile palette, standby pill, and kicker
        // are all built with GM_* tokens directly in the screen module.
        //
        // CAR-312: the top-corner settings button is built flat in the screen
        // module (a bare recolored lv_imgbtn glyph), which reads inconsistent
        // against the other Nothing-theme corner buttons. Give it the round
        // glass fill + icon recolor here, mirroring the ui_MenuScreen_backButton
        // treatment below (same lv_imgbtn idiom, same 44px size).
        //
        // CAR-312 (Codex review): this launcher is *dark-only by design* — the
        // branch below repaints the screen bg back to GM_BG even on
        // UI_THEME_LIGHT non-AMOLED panels. Styling the button with the
        // user-resolved `palette` would, on light theme, render a near-white
        // glass disc (palette.surfaceStrong ≈ 0xFAFAFA) with a black icon
        // (palette.textPrimary) sitting on the dark Nothing canvas — exactly the
        // styling inconsistency this fix is meant to remove. Use a fixed dark
        // palette + the GM_CONTENT mono tone the launcher already uses for its
        // icons, so the button matches regardless of the user's theme setting.
        // Placement is unchanged: the button already sits in the top corner
        // (LV_ALIGN_TOP_RIGHT in the screen module), well clear of the
        // "SELECT MODE" kicker — the CAR-308 rebuild removed the old top dial
        // labels CAR-312 called out, so there is no longer anything to crowd.
        if (lv_obj_is_valid(ui_ModeScreen_settingsButton)) {
            const DisplayPalette darkPalette = makeDisplayPalette(UI_THEME_DEFAULT, false);
            styleRoundIconButton(ui_ModeScreen_settingsButton, darkPalette, GM_CONTENT, 44);
        }
        //
        // CAR-308 P2 #1 (Codex review): the launcher is *dark-only by design*
        // — tiles use white@10% bg + GM_FAINT borders, icons are
        // accent-recolored against a black canvas, and the standby pill +
        // kicker resolve mono labels in GM_MUTED. There is no light-theme
        // palette equivalent (the design handoff is single-theme), and the
        // legacy styleMenuTile() retint machinery was deliberately removed
        // when the screen was rebuilt. styleScreenBase() above repaints the
        // screen bg to palette.surface, which on UI_THEME_LIGHT (non-AMOLED
        // panels) is near-white — that makes the white@10% tiles vanish into
        // the background. Restore GM_BG here so the launcher always renders
        // on the dark Nothing canvas regardless of the user's theme setting.
        // Mirrors the StandbyScreen pattern (also dark-only by design).
        lv_obj_set_style_bg_color(activeScreen, GM_BG, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_color(activeScreen, GM_BG, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(activeScreen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (activeScreen == ui_MenuScreen) {
        // CAR-279: Quick-settings list is built from gm_make_screen + GM_*
        // tokens (dark-themed) in ui_MenuScreen.c. styleScreenBase() above
        // repaints the screen background to palette.surfaceBase, so on
        // UI_THEME_LIGHT the row text/icons would be near-white on near-white
        // without an explicit palette pass. Recolor the back button (round)
        // and delegate row/kicker/stepper recolor to the screen module.
        if (lv_obj_is_valid(ui_MenuScreen_backButton)) {
            styleRoundIconButton(ui_MenuScreen_backButton, palette, palette.textPrimary, 44);
        }
        ui_MenuScreen_apply_palette(palette.textPrimary, palette.textMuted, palette.surface, palette.accent);
    } else if (activeScreen == ui_StandbyScreen) {
        // Nothing-theme standby (CAR-277) styles itself via the gm_* builders in
        // ui_StandbyScreen_screen_init; nothing to restyle here. The kicker text
        // and clock/wifi/bt are driven by the standby effect + updateStandbyScreen().
    }
}

void DefaultUI::updateStandbyScreen() {
    if (standbyEnterTime > 0) {
        const Settings &settings = controller->getSettings();
        const unsigned long now = millis();
        if (now - standbyEnterTime >= settings.getStandbyBrightnessTimeout()) {
            setBrightness(settings.getStandbyBrightness());
        }
    }

    // CAR-304: WiFi+NTP are independent of controller BLE pairing — keep the
    // hero clock visible while waitingForController so the kicker
    // "WAITING FOR CONTROLLER" doesn't leave a blank space where time should
    // be. Other gates (error/updating/autotuning/AP/no-WiFi) still suppress
    // the clock because those states genuinely invalidate it.
    // CAR-306: also drop the `initialized` gate. `initialized` is only set
    // true on `controller:bluetooth:connect`, so on a cold boot with WiFi/NTP
    // up but no controller yet paired, the clock would stay hidden — exactly
    // the case CAR-304 was meant to cover. Pairing state is conveyed by the
    // kicker label and the BT icon recolor, not the clock.
    if (!apActive && WiFi.status() == WL_CONNECTED && !updateActive && !error && !autotuning) {
        time_t now;
        struct tm timeinfo;

        localtime_r(&now, &timeinfo);
        if (getLocalTime(&timeinfo, 500)) {
            Settings &settings = controller->getSettings();
            const bool is24h = settings.isClock24hFormat();
            const int hour = is24h ? timeinfo.tm_hour : ((timeinfo.tm_hour % 12 == 0) ? 12 : timeinfo.tm_hour % 12);
            lv_label_set_text_fmt(ui_StandbyScreen_time, "%02d:#D71921 %02d#", hour, timeinfo.tm_min);
            lv_obj_clear_flag(ui_StandbyScreen_time, LV_OBJ_FLAG_HIDDEN);

            // ndot_120 only covers digits+colon; drive the AM/PM suffix separately
            if (lv_obj_is_valid(gm_h.ampm)) {
                if (!is24h) {
                    char ampm[3]; // "AM\0" / "PM\0"
                    strftime(ampm, sizeof(ampm), "%p", &timeinfo);
                    lv_label_set_text(gm_h.ampm, ampm);
                    lv_obj_clear_flag(gm_h.ampm, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(gm_h.ampm, LV_OBJ_FLAG_HIDDEN);
                }
            }

            christmasMode = (timeinfo.tm_mon == 11 && timeinfo.tm_mday < 27) || (timeinfo.tm_mon == 0 && timeinfo.tm_mday < 6);
        } else {
            // WiFi up but NTP hasn't synced yet — getLocalTime() returns false.
            // Without this branch the clock would keep its prior text (often the
            // screen-init "--:--"), which renders as missing-glyph rectangles in
            // ndot_120. Hide it like the no-WiFi path does. (CAR-299)
            lv_obj_add_flag(ui_StandbyScreen_time, LV_OBJ_FLAG_HIDDEN);
            if (lv_obj_is_valid(gm_h.ampm)) {
                lv_obj_add_flag(gm_h.ampm, LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {
        // ndot_120 only covers 0x30-0x3A (digits + colon); dashes in "--:--"
        // render as LVGL missing-glyph rectangles. Hide the clock instead and
        // let the kicker label convey state (CAR-299).
        lv_obj_add_flag(ui_StandbyScreen_time, LV_OBJ_FLAG_HIDDEN);
        if (lv_obj_is_valid(gm_h.ampm)) {
            lv_obj_add_flag(gm_h.ampm, LV_OBJ_FLAG_HIDDEN);
        }
    }
    // WiFi/BT icons always visible — color indicates connection state.
    lv_obj_set_style_img_recolor(ui_StandbyScreen_wifiIcon,
        (!apActive && WiFi.status() == WL_CONNECTED) ? GM_CONTENT : GM_MUTED, 0);
    lv_obj_set_style_img_recolor_opa(ui_StandbyScreen_wifiIcon, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_StandbyScreen_wifiIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_img_recolor(ui_StandbyScreen_bluetoothIcon,
        controller->getClientController()->isConnected() ? GM_CONTENT : GM_MUTED, 0);
    lv_obj_set_style_img_recolor_opa(ui_StandbyScreen_bluetoothIcon, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_StandbyScreen_bluetoothIcon, LV_OBJ_FLAG_HIDDEN);

    // Temperature sub-line (Nothing theme): "<current>° / <target>°C".
    // CAR-303: Controller::getTargetTemp() returns 0 in MODE_STANDBY by
    // design (no active process), so targetTemp would always be 0 here and
    // the line fell through to "--". Fall back to the active brew profile's
    // setpoint — that's what wakeup will heat the boiler to.
    // Only render live values when the BLE controller is connected; otherwise
    // currentTemp is the stale initial sensor value (refreshed only by BLE
    // callbacks) and the profile-setpoint fallback would mislead (e.g.
    // "0° / 93°C" while the kicker says WAITING FOR CONTROLLER).
    if (lv_obj_is_valid(gm_h.standby_temp)) {
        bool ctrlConnected = controller->getClientController()->isConnected();
        int displayTarget = targetTemp;
        if (displayTarget <= 0) {
            displayTarget = static_cast<int>(profileManager->getSelectedProfile().temperature);
        }
        if (ctrlConnected && displayTarget > 0) {
            lv_label_set_text_fmt(gm_h.standby_temp, "%d\xC2\xB0 / %d\xC2\xB0\x43", currentTemp, displayTarget);
        } else {
            lv_label_set_text(gm_h.standby_temp, "--");
        }
    }
}

// CAR-308 P2 #2 (Codex review): shared status-bar clock formatter. Called
// from updateStatusScreen() and from loop() for every screen that owns a
// gm_status_bar() and points gm_h.status_time at its own clock label
// (StatusScreen, BrewScreen, GrindScreen, ModeScreen). NULL/invalid handle
// is a no-op so callers never have to guard. Format and gating mirror the
// pre-CAR-308 inline block from updateStatusScreen().
void DefaultUI::updateStatusBarClock() {
    if (gm_h.status_time == nullptr || !lv_obj_is_valid(gm_h.status_time)) {
        return;
    }
    // CAR-315: gm_status_bar() seeds the label with a "--:--" placeholder in
    // spacemono_14 and hides it until a real time is available. Mirror the
    // CAR-299 StandbyScreen handling: only show the clock once NTP has given us
    // a plausible time (epoch past 2021), otherwise keep it hidden so the
    // placeholder never renders as missing-glyph rectangles on any screen.
    time_t nowEpoch = time(nullptr);
    struct tm timeinfo;
    if (nowEpoch > 1609459200 && localtime_r(&nowEpoch, &timeinfo) != nullptr) {
        char buf[8];
        strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
        lv_label_set_text(gm_h.status_time, buf);
        lv_obj_clear_flag(gm_h.status_time, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(gm_h.status_time, LV_OBJ_FLAG_HIDDEN);
    }
}

void DefaultUI::updateStatusScreen() {
    // CAR-278: status screen rebuilt in the Nothing theme. Single screen
    // with three modes — brew (red), steam (gold), water (blue) — driven by
    // gm_status_apply_mode(). All widgets we touch live on `gm_h`.
    if (lv_scr_act() != ui_StatusScreen) {
        return;
    }

    ensureStatusBeanLabel();

    // Status bar clock — matches the standby pattern. Shared with ModeScreen
    // (and any other screen that owns a gm_status_bar() and wires
    // gm_h.status_time on init) via the updateStatusBarClock() helper.
    updateStatusBarClock();

    // Mode-driven defaults. Pull a fresh process snapshot once.
    ProcessSnapshot proc = controller->getProcessSnapshot();
    int arcPct = 0;
    int barPct = 0;

    auto setHero = [&](const char *value, const char *unit) {
        if (gm_h.hero != nullptr && lv_obj_is_valid(gm_h.hero)) {
            lv_label_set_text(gm_h.hero, value);
        }
        if (gm_h.hero_unit != nullptr && lv_obj_is_valid(gm_h.hero_unit)) {
            lv_label_set_text(gm_h.hero_unit, unit);
            // lv_obj_align_to is one-shot in LVGL v8; re-anchor the unit suffix
            // every frame so it tracks the hero label's changing text width.
            if (gm_h.hero != nullptr && lv_obj_is_valid(gm_h.hero)) {
                lv_obj_align_to(gm_h.hero_unit, gm_h.hero, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -10);
            }
        }
    };

    auto setKicker = [&](const char *txt) {
        if (gm_h.kicker != nullptr && lv_obj_is_valid(gm_h.kicker)) {
            lv_label_set_text(gm_h.kicker, txt);
        }
    };

    // setStatus() removed (CAR-278 review #1): gm_h.status_label is owned by
    // ui_StandbyScreen (assigned in its _screen_init, NULLed in _screen_destroy)
    // and is not visible from the Status screen. Active-vs-idle on the Status
    // screen is conveyed by the kicker, READY pill, and progress bar
    // (mode-dependent). Do not write to gm_h.status_label from this path.

    if (mode == MODE_BREW) {
        // Hero is brew elapsed time (m:ss); the unit slot stays as 's'.
        unsigned long now = millis();
        if (proc.exists && !proc.isActive && proc.finished > 0) {
            now = proc.finished;
        }
        unsigned long elapsedMs = 0;
        if (proc.exists && proc.started > 0 && now >= proc.started) {
            elapsedMs = now - proc.started;
        }
        const auto elapsedSecs = static_cast<int>(elapsedMs / 1000);
        const int mm = elapsedSecs / 60;
        const int ss = elapsedSecs % 60;
        char heroBuf[16];
        snprintf(heroBuf, sizeof(heroBuf), "%d:%02d", mm, ss);
        setHero(heroBuf, "s");

        // Kicker: phase name, falling back to BREW.
        if (proc.exists && proc.isActive && proc.phaseName[0] != '\0') {
            String phaseUpper = String(proc.phaseName);
            phaseUpper.toUpperCase();
            setKicker(phaseUpper.c_str());
        } else if (proc.exists && !proc.isActive && proc.finished > 0) {
            setKicker("FINISHED");
        } else {
            setKicker("BREW");
        }

        // Metric row: WEIGHT / TEMP / PRESS / FLOW (flow shows '--').
        if (gm_h.m_weight != nullptr && lv_obj_is_valid(gm_h.m_weight)) {
            lv_label_set_text_fmt(gm_h.m_weight, "%.1fg",
                                  proc.exists ? proc.currentVolume : 0.0);
        }
        if (gm_h.m_temp != nullptr && lv_obj_is_valid(gm_h.m_temp)) {
            lv_label_set_text_fmt(gm_h.m_temp, "%d\xC2\xB0", currentTemp);
        }
        if (gm_h.m_press != nullptr && lv_obj_is_valid(gm_h.m_press)) {
            lv_label_set_text_fmt(gm_h.m_press, "%.1f", pressure);
        }
        if (gm_h.m_flow != nullptr && lv_obj_is_valid(gm_h.m_flow)) {
            // ProcessSnapshot has no flow rate field; show placeholder.
            lv_label_set_text(gm_h.m_flow, "--");
        }

        // CAR-300: brew uses linear bar (not arc) for dose/phase progress.
        // Prefer volumetric fill, else phase elapsed time, else stay at 0.
        if (proc.exists && proc.target == ProcessTarget::VOLUMETRIC && proc.hasVolumetricTarget &&
            proc.volumetricTargetValue > 0.0) {
            barPct = static_cast<int>((proc.currentVolume / proc.volumetricTargetValue) * 100.0);
        } else if (proc.exists && proc.currentPhaseStarted > 0 && now >= proc.currentPhaseStarted &&
                   proc.phaseDuration > 0) {
            const unsigned long progress = now - proc.currentPhaseStarted;
            barPct = static_cast<int>((progress * 100UL) / proc.phaseDuration);
        }
    } else if (mode == MODE_STEAM) {
        // Hero is current temp; READY pill appears when stable.
        char heroBuf[16];
        snprintf(heroBuf, sizeof(heroBuf), "%d", currentTemp);
        setHero(heroBuf, "\xC2\xB0");
        setKicker("STEAM");

        if (gm_h.m_temp != nullptr && lv_obj_is_valid(gm_h.m_temp)) {
            lv_label_set_text_fmt(gm_h.m_temp, "%d\xC2\xB0", targetTemp);
        }

        // Arc tracks heat-up progress toward target. Cap at 99 until the
        // temperature stability flag fires so the arc cannot read full while
        // the READY pill is still hidden during thermal overshoot
        // (review-comment 4585899391, finding #2).
        if (targetTemp > 0) {
            arcPct = static_cast<int>((static_cast<float>(currentTemp) / targetTemp) * 100.0f);
            if (!isTemperatureStable && arcPct > 99) {
                arcPct = 99;
            }
        }
        // bar_pct >= 100 toggles the READY pill in gm_status_apply_mode.
        barPct = isTemperatureStable ? 100 : 0;
    } else if (mode == MODE_WATER) {
        // Hero is water dispensed (g); horizontal bar shows progress.
        const double dispensed = proc.exists ? proc.currentVolume : 0.0;
        char heroBuf[16];
        snprintf(heroBuf, sizeof(heroBuf), "%.0f", dispensed);
        setHero(heroBuf, "g");
        setKicker("WATER");

        const double target =
            proc.exists && proc.hasVolumetricTarget && proc.volumetricTargetValue > 0.0
                ? proc.volumetricTargetValue
                : 0.0;
        if (gm_h.w_target != nullptr && lv_obj_is_valid(gm_h.w_target)) {
            if (target > 0.0) {
                lv_label_set_text_fmt(gm_h.w_target, "%.0fg", target);
            } else {
                lv_label_set_text(gm_h.w_target, "--");
            }
        }
        if (gm_h.w_temp != nullptr && lv_obj_is_valid(gm_h.w_temp)) {
            lv_label_set_text_fmt(gm_h.w_temp, "%d\xC2\xB0", currentTemp);
        }
        if (gm_h.w_flow != nullptr && lv_obj_is_valid(gm_h.w_flow)) {
            lv_label_set_text(gm_h.w_flow, "--");
        }

        if (target > 0.0) {
            barPct = static_cast<int>((dispensed / target) * 100.0);
        }
    } else {
        // Fallback (shouldn't hit on this screen): zero everything.
        setHero("--", "");
        setKicker("");
    }

    // CAR-300: context label always shows in brew mode (profile · pressure).
    // When a bean is selected, append it after the profile/pressure line.
    if (statusBeanLabel != nullptr && lv_obj_is_valid(statusBeanLabel)) {
        if (mode == MODE_BREW) {
            lv_obj_clear_flag(statusBeanLabel, LV_OBJ_FLAG_HIDDEN);
            if (!selectedBean.isEmpty()) {
                lv_label_set_text_fmt(statusBeanLabel, "%s · %.1f bar · %s",
                                      selectedProfile.label.c_str(), pressure,
                                      selectedBean.c_str());
            } else {
                lv_label_set_text_fmt(statusBeanLabel, "%s · %.1f bar",
                                      selectedProfile.label.c_str(), pressure);
            }
        } else {
            lv_obj_add_flag(statusBeanLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Drive widget visibility + accent retint per mode.
    gm_status_apply_mode(mode, arcPct, barPct);
}

void DefaultUI::adjustDials(lv_obj_t *dials) {
    const DisplayPalette palette = makeDisplayPalette(controller->getSettings().getThemeMode(), AmoledDisplayDriver::getInstance() == panelDriver);
    const bool roundDisplay = isRoundDisplay();
    const RingVisualContext ringContext{mode, currentTemp, targetTemp, active != 0, grindActive != 0, controller,
                                        isTemperatureStable != 0};
    const RingVisual ringVisual = buildRingVisual(palette, ringContext);
    lv_obj_t *tempGauge = ui_comp_get_child(dials, UI_COMP_DIALS_TEMPGAUGE);
    lv_obj_t *tempText = ui_comp_get_child(dials, UI_COMP_DIALS_TEMPTEXT);
    lv_obj_t *tempTarget = ui_comp_get_child(dials, UI_COMP_DIALS_TEMPTARGET);
    lv_obj_t *tempIcon = ui_comp_get_child(dials, UI_COMP_DIALS_TEMPICON);
    lv_obj_t *pressureTarget = ui_comp_get_child(dials, UI_COMP_DIALS_PRESSURETARGET);
    lv_obj_t *pressureGauge = ui_comp_get_child(dials, UI_COMP_DIALS_PRESSUREGAUGE);
    lv_obj_t *pressureText = ui_comp_get_child(dials, UI_COMP_DIALS_PRESSURETEXT);
    lv_obj_t *pressureSymbol = ui_comp_get_child(dials, UI_COMP_DIALS_IMAGE6);
    _ui_flag_modify(pressureTarget, LV_OBJ_FLAG_HIDDEN, pressureAvailable);
    _ui_flag_modify(pressureGauge, LV_OBJ_FLAG_HIDDEN, pressureAvailable);
    _ui_flag_modify(pressureText, LV_OBJ_FLAG_HIDDEN, pressureAvailable);
    _ui_flag_modify(pressureSymbol, LV_OBJ_FLAG_HIDDEN, pressureAvailable);
    lv_obj_set_x(tempText, pressureAvailable ? -50 : 0);
    lv_obj_set_y(tempText, pressureAvailable ? -205 : -180);
    lv_arc_set_range(pressureGauge, 0, pressureScaling * 10);
    applyProcessRing(tempGauge, palette, ringVisual, roundDisplay);

    if (roundDisplay) {
        lv_obj_set_size(dials, 466, 466);
        lv_obj_align(dials, LV_ALIGN_CENTER, 0, 0);

        styleDialRing(pressureGauge, palette, palette.grind, true, true);
        applyProcessRing(tempGauge, palette, ringVisual, true);
        lv_arc_set_bg_angles(pressureGauge, 300, 60);

        lv_obj_set_style_text_color(tempText, palette.textPrimary, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(tempText, &ndot_34, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(tempText, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(pressureText, pressureAvailable ? palette.accent : palette.textMuted, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(pressureText, &ndot_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(pressureText, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_align(tempText, LV_ALIGN_TOP_MID, 0, 92);
        lv_obj_align(tempIcon, LV_ALIGN_TOP_MID, -96, 102);
        lv_img_set_zoom(tempIcon, 130);
        lv_obj_set_style_img_recolor(tempIcon, isTemperatureStable ? palette.success : palette.accent, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_img_recolor_opa(tempIcon, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

        if (pressureAvailable) {
            lv_obj_align(pressureText, LV_ALIGN_BOTTOM_MID, 0, -94);
            lv_obj_align(pressureSymbol, LV_ALIGN_BOTTOM_MID, -92, -88);
            lv_img_set_zoom(pressureSymbol, 130);
            lv_obj_set_style_img_recolor(pressureSymbol, palette.accent, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor_opa(pressureSymbol, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        if (tempTarget != nullptr && lv_obj_is_valid(tempTarget)) {
            lv_obj_set_style_img_recolor(tempTarget, palette.textPrimary, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor_opa(tempTarget, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (pressureTarget != nullptr && lv_obj_is_valid(pressureTarget)) {
            lv_obj_set_style_img_recolor(pressureTarget, palette.grind, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor_opa(pressureTarget, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

inline void DefaultUI::adjustTempTarget(lv_obj_t *dials) {
    double gaugeAngle = pressureAvailable ? 124.0 : 304;
    double gaugeStart = pressureAvailable ? 118.0 : -62;
    double percentage = static_cast<double>(targetTemp) / 160.0;
    lv_obj_t *tempTarget = ui_comp_get_child(dials, UI_COMP_DIALS_TEMPTARGET);
    adjustTarget(tempTarget, percentage, gaugeStart, gaugeAngle);
}

void DefaultUI::applyTheme() {
    const Settings &settings = controller->getSettings();
    const bool amoledPanel = AmoledDisplayDriver::getInstance() == panelDriver;
    int newThemeMode = resolveDisplayThemeMode(settings.getThemeMode(), amoledPanel);

    if (newThemeMode != currentThemeMode) {
        currentThemeMode = newThemeMode;
        ui_theme_set(currentThemeMode);

        if (amoledPanel && currentThemeMode == UI_THEME_DEFAULT) {
            enable_amoled_black_theme_override(lv_disp_get_default());
        }
    }
}

void DefaultUI::adjustTarget(lv_obj_t *obj, double percentage, double start, double range) const {
    double angle = start + range - range * percentage;

    lv_img_set_angle(obj, angle * -10);
    int x = static_cast<int>(std::cos(angle * M_PI / 180.0f) * 235.0);
    int y = static_cast<int>(std::sin(angle * M_PI / 180.0f) * -235.0);
    lv_obj_set_pos(obj, x, y);
}

void DefaultUI::loopTask(void *arg) {
    auto *ui = static_cast<DefaultUI *>(arg);
    while (true) {
        ui->loop();
        vTaskDelay(25 / portTICK_PERIOD_MS);
    }
}

void DefaultUI::profileLoopTask(void *arg) {
    auto *ui = static_cast<DefaultUI *>(arg);
    while (true) {
        ui->loopProfiles();
        vTaskDelay(25 / portTICK_PERIOD_MS);
    }
}

// ── Auto-steam toggle ─────────────────────────────────────────────────────

void DefaultUI::ensureAutoSteamButton() {
    if (lv_scr_act() != ui_BrewScreen || !lv_obj_is_valid(ui_BrewScreen))
        return;

    const bool amoledPanel = AmoledDisplayDriver::getInstance() == panelDriver;
    const DisplayPalette palette = makeDisplayPalette(controller->getSettings().getThemeMode(), amoledPanel);

    if (autoSteamBtn == nullptr || !lv_obj_is_valid(autoSteamBtn)) {
        autoSteamBtn = lv_btn_create(ui_BrewScreen);
        if (autoSteamBtn == nullptr)
            return;
        lv_obj_set_size(autoSteamBtn, 88, 36);
        lv_obj_align(autoSteamBtn, LV_ALIGN_TOP_RIGHT, -14, 56);
        lv_obj_set_style_radius(autoSteamBtn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(autoSteamBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *label = lv_label_create(autoSteamBtn);
        lv_label_set_text(label, "AutoSteam");
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, 76);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(label);

        lv_obj_add_event_cb(autoSteamBtn, autoSteamBtnCb, LV_EVENT_CLICKED, this);
    }

    // Update colors to reflect current state
    const lv_color_t activeBg = autoSteamEnabled ? palette.accentCool : palette.surfaceElevated;
    const lv_color_t activeBorder = autoSteamEnabled ? palette.accentCool : palette.surfaceOutline;
    const lv_color_t activeText = autoSteamEnabled ? palette.surface : palette.textMuted;
    lv_obj_set_style_bg_color(autoSteamBtn, activeBg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(autoSteamBtn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(autoSteamBtn, activeBorder, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(autoSteamBtn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *label = lv_obj_get_child(autoSteamBtn, 0);
    if (label != nullptr && lv_obj_is_valid(label)) {
        lv_obj_set_style_text_color(label, activeText, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void DefaultUI::autoSteamBtnCb(lv_event_t *e) {
    auto *ui = static_cast<DefaultUI *>(lv_event_get_user_data(e));
    ui->autoSteamEnabled = !ui->autoSteamEnabled;
    ui->ensureAutoSteamButton();
}

// ── Bean select overlay ────────────────────────────────────────────────────

void DefaultUI::ensureBeanSelectButton() {
    if (lv_scr_act() != ui_BrewScreen || !lv_obj_is_valid(ui_BrewScreen) || beanSelectBtn != nullptr)
        return;

    const bool amoledPanel = AmoledDisplayDriver::getInstance() == panelDriver;
    const DisplayPalette palette = makeDisplayPalette(controller->getSettings().getThemeMode(), amoledPanel);

    beanSelectBtn = lv_btn_create(ui_BrewScreen);
    if (beanSelectBtn == nullptr)
        return;
    lv_obj_set_size(beanSelectBtn, 88, 36);
    lv_obj_align(beanSelectBtn, LV_ALIGN_TOP_RIGHT, -14, 14);
    lv_obj_set_style_bg_color(beanSelectBtn, palette.surfaceElevated, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(beanSelectBtn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(beanSelectBtn, palette.surfaceOutline, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(beanSelectBtn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(beanSelectBtn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(beanSelectBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(beanSelectBtn);
    lv_label_set_text(label, selectedBean.isEmpty() ? "Bean" : selectedBean.c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, 76);
    lv_obj_set_style_text_color(label, palette.textPrimary, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);

    lv_obj_add_event_cb(beanSelectBtn, beanSelectBtnCb, LV_EVENT_CLICKED, this);
}

void DefaultUI::showBeanSelectScreen() {
    cachedBeans = controller->getBeanManager()->listBeans();

    beanSelectScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(beanSelectScreen, lv_color_hex(0x0A0A0A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(beanSelectScreen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(beanSelectScreen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(beanSelectScreen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title = lv_label_create(beanSelectScreen);
    lv_label_set_text(title, "Select Bean");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8E8E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, &ndot_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    lv_obj_t *backBtn = lv_btn_create(beanSelectScreen);
    if (backBtn == nullptr) {
        lv_obj_del(beanSelectScreen);
        beanSelectScreen = nullptr;
        return;
    }
    lv_obj_set_size(backBtn, 52, 52);
    lv_obj_align(backBtn, LV_ALIGN_TOP_LEFT, 14, 10);
    lv_obj_set_style_bg_color(backBtn, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(backBtn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(backBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(backBtn, 999, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *backLabel = lv_label_create(backBtn);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(backLabel, lv_color_hex(0xE8E8E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(backBtn, beanBackCb, LV_EVENT_CLICKED, this);

    lv_obj_t *list = lv_obj_create(beanSelectScreen);
    lv_disp_t *disp = lv_disp_get_default();
    const lv_coord_t dispH = lv_disp_get_ver_res(disp);
    lv_obj_set_size(list, lv_disp_get_hor_res(disp), dispH - 72);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x0A0A0A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(list, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(list, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    // "No bean" row — user_data index = -1 encoded as uintptr_t
    {
        lv_obj_t *item = lv_btn_create(list);
        lv_obj_set_width(item, lv_pct(100));
        lv_obj_set_height(item, 54);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x161616), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(item, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, "— No bean —");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x777777), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl, &ndot_18, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_set_user_data(item, (void *)(uintptr_t)(UINT32_MAX)); // sentinel for "no bean"
        lv_obj_add_event_cb(item, beanItemCb, LV_EVENT_CLICKED, this);
    }

    uintptr_t idx = 0;
    for (const auto &bean : cachedBeans) {
        if (bean.archived) {
            idx++;
            continue;
        }
        lv_obj_t *item = lv_btn_create(list);
        lv_obj_set_width(item, lv_pct(100));
        lv_obj_set_height(item, 54);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x161616), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(item, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, bean.name.c_str());
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, lv_pct(90));
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8E8E8), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl, &ndot_18, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_set_user_data(item, (void *)idx);
        lv_obj_add_event_cb(item, beanItemCb, LV_EVENT_CLICKED, this);
        idx++;
    }

    beanSelectActive = true;
    lv_scr_load(beanSelectScreen);
}

void DefaultUI::beanSelectBtnCb(lv_event_t *e) {
    auto *ui = static_cast<DefaultUI *>(lv_event_get_user_data(e));
    ui->showBeanSelectScreen();
}

void DefaultUI::beanItemCb(lv_event_t *e) {
    auto *ui = static_cast<DefaultUI *>(lv_event_get_user_data(e));
    lv_obj_t *target = lv_event_get_target(e);
    uintptr_t idx = (uintptr_t)lv_obj_get_user_data(target);

    if (idx != (uintptr_t)(UINT32_MAX) && idx < ui->cachedBeans.size()) {
        ui->pluginManager->trigger("beans:selected", "name", ui->cachedBeans[idx].name);
    } else {
        ui->pluginManager->trigger("beans:selected", "name", String(""));
    }

    ui->beanSelectActive = false;
    lv_obj_t *screen = ui->beanSelectScreen;
    ui->beanSelectScreen = nullptr;
    lv_scr_load(*ui->targetScreen);
    lv_obj_del(screen);
    ui->rerender = true;
}

void DefaultUI::beanBackCb(lv_event_t *e) {
    auto *ui = static_cast<DefaultUI *>(lv_event_get_user_data(e));
    ui->beanSelectActive = false;
    lv_obj_t *screen = ui->beanSelectScreen;
    ui->beanSelectScreen = nullptr;
    lv_scr_load(*ui->targetScreen);
    lv_obj_del(screen);
    ui->rerender = true;
}
