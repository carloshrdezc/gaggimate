// CAR-279: ui_MenuScreen rebuilt as the Nothing-theme Quick-settings list.
// The mode-hub role moved to ui_ModeScreen (CAR-291). Layout: kicker on top,
// settings rows (brightness toggle / brew-temp stepper / scale toggle), back
// button -> mode hub via onBackToModeScreen. PRO-597: the BRIGHTNESS toggle is
// now wired to the real persisted mainBrightness setting (full-vs-dimmed boost,
// applied live); the SCALE toggle is HIDDEN because no runtime "scale enabled"
// setting exists to bind it to (BLE scale connection is mode-driven/automatic).
//
// CAR-358: added WATER TEMP + STEAM TEMP stepper rows (mirroring BREW TEMP) and
// an obvious full-width "DONE" exit button at the bottom. With 5 rows + an exit
// the fixed 320x220 panel overflowed the round 480 screen, so the row list is
// now a vertically-SCROLLABLE panel and the DONE button is pinned below it at
// BOTTOM_MID (outside the scroll area) so the exit is always reachable.

#include "../ui.h"
#include "../gm_ui.h"

lv_obj_t *ui_MenuScreen = NULL;
lv_obj_t *ui_MenuScreen_contentPanel = NULL;
lv_obj_t *ui_MenuScreen_backButton = NULL;
lv_obj_t *ui_MenuScreen_doneButton = NULL; // CAR-358: obvious full-width exit
lv_obj_t *ui_MenuScreen_restartButton = NULL;
lv_obj_t *ui_MenuScreen_brightnessSwitch = NULL;
lv_obj_t *ui_MenuScreen_brewTempValue = NULL;
lv_obj_t *ui_MenuScreen_brewTempMinus = NULL;
lv_obj_t *ui_MenuScreen_brewTempPlus = NULL;
// CAR-358: water + steam temp steppers (mirror the brew-temp row).
lv_obj_t *ui_MenuScreen_waterTempValue = NULL;
lv_obj_t *ui_MenuScreen_waterTempMinus = NULL;
lv_obj_t *ui_MenuScreen_waterTempPlus = NULL;
lv_obj_t *ui_MenuScreen_steamTempValue = NULL;
lv_obj_t *ui_MenuScreen_steamTempMinus = NULL;
lv_obj_t *ui_MenuScreen_steamTempPlus = NULL;
lv_obj_t *ui_MenuScreen_scaleSwitch = NULL;

// CAR-279 review fix: track themable children so DefaultUI's palette pass can
// recolor them on UI_THEME_LIGHT. The Quick-settings layout is built from
// gm_make_screen + GM_* tokens (dark-themed) and styleScreenBase() repaints
// the screen background to palette.surfaceBase, so on light themes the
// originally-near-white text/icons go invisible against a near-white bg.
//
// CAR-358: expanded from 3 to 5 rows (added WATER/STEAM TEMP). Stepper +/- glyph
// labels are tracked per-row in qs_stepper_minus_labels / qs_stepper_plus_labels
// (was a single brew-only pair) so every new stepper glyph is recolored too —
// otherwise they go invisible on UI_THEME_LIGHT (the palette pitfall).
#define QS_MAX_ROWS 6
#define QS_MAX_STEPPERS 3
static lv_obj_t *qs_kicker = NULL;
static lv_obj_t *qs_row_icons[QS_MAX_ROWS] = {NULL};
static lv_obj_t *qs_row_labels[QS_MAX_ROWS] = {NULL};
static lv_obj_t *qs_stepper_minus_labels[QS_MAX_STEPPERS] = {NULL};
static lv_obj_t *qs_stepper_plus_labels[QS_MAX_STEPPERS] = {NULL};
// CAR-358 review (Codex P2): each stepper's "+" carries a per-mode accent
// (brew=red, water=blue, steam=gold). Track the "+" button object AND its
// intended accent so the palette pass restores the mode color per-row instead
// of overwriting all three with one shared accent.
static lv_obj_t *qs_stepper_plus_btns[QS_MAX_STEPPERS] = {NULL};
static lv_color_t qs_stepper_plus_accents[QS_MAX_STEPPERS];
static lv_obj_t *qs_done_label = NULL; // CAR-358: DONE button text
static lv_obj_t *qs_restart_label = NULL;
static lv_obj_t *qs_restart_dialog = NULL;
static int qs_row_count = 0;
static int qs_stepper_count = 0;

void ui_event_MenuScreen(lv_event_t *e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_SCREEN_LOADED) {
        onMenuScreenLoad(e);
    }
}

static void ui_event_MenuScreen_back(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onBackToModeScreen(e);
}

static void ui_event_MenuScreen_brewTempLower(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onBrewTempLower(e);
}

static void ui_event_MenuScreen_brewTempRaise(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onBrewTempRaise(e);
}

// CAR-358: water/steam steppers act on Settings directly (mode-independent);
// see onMenu{Water,Steam}Temp{Lower,Raise} in ui_events.cpp. After changing the
// setting we refresh the value label immediately for instant feedback (the
// settings values are not reactive signals like brew's targetTemp, so the
// DefaultUI effect alone would only re-seed on screen (re)activation).
static void ui_event_MenuScreen_waterTempLower(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    onMenuWaterTempLower(e);
    if (lv_obj_is_valid(ui_MenuScreen_waterTempValue))
        lv_label_set_text_fmt(ui_MenuScreen_waterTempValue, "%d", gmGetWaterTempSetting());
}

static void ui_event_MenuScreen_waterTempRaise(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    onMenuWaterTempRaise(e);
    if (lv_obj_is_valid(ui_MenuScreen_waterTempValue))
        lv_label_set_text_fmt(ui_MenuScreen_waterTempValue, "%d", gmGetWaterTempSetting());
}

static void ui_event_MenuScreen_steamTempLower(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    onMenuSteamTempLower(e);
    if (lv_obj_is_valid(ui_MenuScreen_steamTempValue))
        lv_label_set_text_fmt(ui_MenuScreen_steamTempValue, "%d", gmGetSteamTempSetting());
}

static void ui_event_MenuScreen_steamTempRaise(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    onMenuSteamTempRaise(e);
    if (lv_obj_is_valid(ui_MenuScreen_steamTempValue))
        lv_label_set_text_fmt(ui_MenuScreen_steamTempValue, "%d", gmGetSteamTempSetting());
}

// PRO-597: wire the BRIGHTNESS switch to the real persisted mainBrightness
// setting (full-vs-dimmed boost). Reads the toggle state and pushes it through
// the C-linkage bridge (gmSetBrightnessBoost in ui_events.cpp), which persists
// via Settings and applies the new level to the panel live. Was a visual-only
// no-op stub (CAR-279).
static void ui_event_MenuScreen_brightness(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *sw = lv_event_get_target(e);
    gmSetBrightnessBoost(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void ui_event_MenuScreen_restart_close(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (qs_restart_dialog != NULL && lv_obj_is_valid(qs_restart_dialog)) lv_obj_del(qs_restart_dialog);
    qs_restart_dialog = NULL;
}

static void ui_event_MenuScreen_restart_confirm(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    onRestartDisplayConfirm(e);
}

static void ui_event_MenuScreen_restart(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (qs_restart_dialog != NULL && lv_obj_is_valid(qs_restart_dialog)) return;

    qs_restart_dialog = lv_obj_create(ui_MenuScreen);
    lv_obj_set_size(qs_restart_dialog, 272, 174);
    lv_obj_center(qs_restart_dialog);
    lv_obj_set_style_bg_color(qs_restart_dialog, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(qs_restart_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qs_restart_dialog, 1, 0);
    lv_obj_set_style_border_color(qs_restart_dialog, GM_CONTENT, 0);
    lv_obj_set_style_border_opa(qs_restart_dialog, LV_OPA_40, 0);
    lv_obj_set_style_radius(qs_restart_dialog, 16, 0);
    lv_obj_clear_flag(qs_restart_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(qs_restart_dialog);
    lv_label_set_text(title, "RESTART DISPLAY?");
    lv_obj_set_style_text_font(title, &grotesk_16, 0);
    lv_obj_set_style_text_color(title, GM_CONTENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    const bool allowed = gmCanRestartDisplay();
    lv_obj_t *message = lv_label_create(qs_restart_dialog);
    lv_label_set_text(message, allowed ? "The display will restart now." : "Restart unavailable while machine is busy.");
    lv_obj_set_width(message, 236);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(message, &grotesk_16, 0);
    lv_obj_set_style_text_color(message, allowed ? GM_MUTED : GM_RED, 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t *cancel = lv_btn_create(qs_restart_dialog);
    lv_obj_set_size(cancel, 108, 40);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 12, -12);
    lv_obj_set_style_bg_color(cancel, GM_BG, 0);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(cancel, ui_event_MenuScreen_restart_close, LV_EVENT_ALL, NULL);
    lv_obj_t *cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "CANCEL");
    lv_obj_set_style_text_font(cancel_label, &grotesk_16, 0);
    lv_obj_set_style_text_color(cancel_label, GM_CONTENT, 0);
    lv_obj_center(cancel_label);

    if (allowed) {
        lv_obj_t *confirm = lv_btn_create(qs_restart_dialog);
        lv_obj_set_size(confirm, 108, 40);
        lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
        lv_obj_set_style_bg_color(confirm, GM_RED, 0);
        lv_obj_set_style_bg_opa(confirm, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(confirm, ui_event_MenuScreen_restart_confirm, LV_EVENT_ALL, NULL);
        lv_obj_t *confirm_label = lv_label_create(confirm);
        lv_label_set_text(confirm_label, "RESTART");
        lv_obj_set_style_text_font(confirm_label, &grotesk_16, 0);
        lv_obj_set_style_text_color(confirm_label, GM_CONTENT, 0);
        lv_obj_center(confirm_label);
    }
}

// Helper: build a settings row (icon + label, no control yet). Captures the
// icon and label refs into the qs_row_icons / qs_row_labels arrays so the
// palette pass can recolor them on theme change. Returns the row container so
// the caller can append a control (switch / stepper buttons).
static lv_obj_t *qs_row(lv_obj_t *parent, const lv_img_dsc_t *icon, const char *label) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 52);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *ic = lv_img_create(row);
    lv_img_set_src(ic, icon);
    lv_obj_set_style_img_recolor(ic, GM_CONTENT, 0);
    lv_obj_set_style_img_recolor_opa(ic, LV_OPA_COVER, 0);

    lv_obj_t *lab = lv_label_create(row);
    lv_label_set_text(lab, label);
    lv_obj_set_style_text_font(lab, &grotesk_16, 0);
    lv_obj_set_style_text_color(lab, GM_CONTENT, 0);
    lv_obj_set_flex_grow(lab, 1);

    if (qs_row_count < QS_MAX_ROWS) {
        qs_row_icons[qs_row_count] = ic;
        qs_row_labels[qs_row_count] = lab;
        qs_row_count++;
    }
    return row;
}

// Helper: build a temp stepper row ( [-]  NN  [+] ). Mirrors the original
// inline brew-temp row (CAR-279) so brew/water/steam are identical. Writes the
// created widgets back through the out-pointers and tracks the +/- glyph
// labels for the palette pass. `accent` colours the "+" button (matches the
// mode accent: red/blue/gold).
static void qs_stepper_row(lv_obj_t *parent, const lv_img_dsc_t *icon, const char *label,
                           lv_color_t accent, lv_event_cb_t minus_cb, lv_event_cb_t plus_cb,
                           lv_obj_t **out_minus, lv_obj_t **out_value, lv_obj_t **out_plus) {
    lv_obj_t *row = qs_row(parent, icon, label);

    lv_obj_t *minus = lv_btn_create(row);
    lv_obj_set_size(minus, 38, 38);
    lv_obj_set_style_radius(minus, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(minus, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(minus, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(minus, 12);
    lv_obj_t *minusLab = lv_label_create(minus);
    lv_label_set_text(minusLab, "-");
    lv_obj_set_style_text_font(minusLab, &grotesk_28, 0);
    lv_obj_set_style_text_color(minusLab, GM_CONTENT, 0);
    lv_obj_center(minusLab);
    lv_obj_add_event_cb(minus, minus_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *value = lv_label_create(row);
    // Initial placeholder; overridden when the screen activates by the
    // DefaultUI menu effect (brew = targetTemp signal; water/steam = the
    // current Settings values).
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_font(value, &ndot_28, 0);
    lv_obj_set_style_text_color(value, GM_CONTENT, 0);

    lv_obj_t *plus = lv_btn_create(row);
    lv_obj_set_size(plus, 38, 38);
    lv_obj_set_style_radius(plus, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(plus, accent, 0);
    lv_obj_set_style_bg_opa(plus, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(plus, 12);
    lv_obj_t *plusLab = lv_label_create(plus);
    lv_label_set_text(plusLab, "+");
    lv_obj_set_style_text_font(plusLab, &grotesk_28, 0);
    lv_obj_set_style_text_color(plusLab, GM_CONTENT, 0);
    lv_obj_center(plusLab);
    lv_obj_add_event_cb(plus, plus_cb, LV_EVENT_ALL, NULL);

    if (qs_stepper_count < QS_MAX_STEPPERS) {
        qs_stepper_minus_labels[qs_stepper_count] = minusLab;
        qs_stepper_plus_labels[qs_stepper_count] = plusLab;
        qs_stepper_plus_btns[qs_stepper_count] = plus;
        qs_stepper_plus_accents[qs_stepper_count] = accent;
        qs_stepper_count++;
    }

    *out_minus = minus;
    *out_value = value;
    *out_plus = plus;
}

void ui_MenuScreen_screen_init(void) {
    ui_MenuScreen = gm_make_screen();
    lv_obj_add_event_cb(ui_MenuScreen, scr_unloaded_delete_cb, LV_EVENT_SCREEN_UNLOADED, ui_MenuScreen_screen_destroy);

    qs_row_count = 0;
    qs_stepper_count = 0;

    // Kicker
    qs_kicker = gm_kicker(ui_MenuScreen, "QUICK SETTINGS", GM_MUTED);

    // Back button (top-left round) -> mode hub. Kept alongside the new bottom
    // DONE button (CAR-358): the round back arrow remains for users who reach
    // for the corner, but the obvious exit is the full-width DONE pill below.
    ui_MenuScreen_backButton = lv_imgbtn_create(ui_MenuScreen);
    lv_imgbtn_set_src(ui_MenuScreen_backButton, LV_IMGBTN_STATE_RELEASED, NULL, &gm_ic_back, NULL);
    lv_obj_set_size(ui_MenuScreen_backButton, 44, 44);
    lv_obj_align(ui_MenuScreen_backButton, LV_ALIGN_TOP_LEFT, 28, 28);
    lv_obj_set_style_img_recolor(ui_MenuScreen_backButton, GM_CONTENT, 0);
    lv_obj_set_style_img_recolor_opa(ui_MenuScreen_backButton, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(ui_MenuScreen_backButton, ui_event_MenuScreen_back, LV_EVENT_ALL, NULL);

    // Settings list panel — VERTICALLY SCROLLABLE (CAR-358). 300px wide keeps
    // full-width rows inside the round bezel; 236px tall leaves room above for
    // the kicker and below for the DONE button. The 6 rows overflow the panel,
    // so vertical scrolling is enabled with a
    // snap so rows don't get stranded half-off. Width/centering keeps content
    // off the curved edges.
    ui_MenuScreen_contentPanel = lv_obj_create(ui_MenuScreen);
    lv_obj_remove_style_all(ui_MenuScreen_contentPanel);
    lv_obj_set_size(ui_MenuScreen_contentPanel, 300, 236);
    lv_obj_align(ui_MenuScreen_contentPanel, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_flex_flow(ui_MenuScreen_contentPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_MenuScreen_contentPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ui_MenuScreen_contentPanel, 8, 0);
    lv_obj_set_style_pad_top(ui_MenuScreen_contentPanel, 4, 0);
    lv_obj_set_style_pad_bottom(ui_MenuScreen_contentPanel, 4, 0);
    // Enable vertical scrolling so all rows are reachable on the round screen.
    lv_obj_add_flag(ui_MenuScreen_contentPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_MenuScreen_contentPanel, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(ui_MenuScreen_contentPanel, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(ui_MenuScreen_contentPanel, LV_SCROLLBAR_MODE_AUTO);

    // Row 1: BRIGHTNESS toggle
    {
        lv_obj_t *row = qs_row(ui_MenuScreen_contentPanel, &gm_ic_sun, "BRIGHTNESS");
        ui_MenuScreen_brightnessSwitch = lv_switch_create(row);
        lv_obj_set_size(ui_MenuScreen_brightnessSwitch, 56, 28);
        lv_obj_set_ext_click_area(ui_MenuScreen_brightnessSwitch, 12);
        lv_obj_add_event_cb(ui_MenuScreen_brightnessSwitch, ui_event_MenuScreen_brightness, LV_EVENT_VALUE_CHANGED, NULL);
    }

    // Row 2: BREW TEMP stepper (red accent on +).
    qs_stepper_row(ui_MenuScreen_contentPanel, &gm_ic_thermo, "BREW TEMP", GM_RED,
                   ui_event_MenuScreen_brewTempLower, ui_event_MenuScreen_brewTempRaise,
                   &ui_MenuScreen_brewTempMinus, &ui_MenuScreen_brewTempValue, &ui_MenuScreen_brewTempPlus);

    // Row 3: WATER TEMP stepper (blue accent on +). CAR-358.
    qs_stepper_row(ui_MenuScreen_contentPanel, &gm_ic_thermo, "WATER TEMP", GM_BLUE,
                   ui_event_MenuScreen_waterTempLower, ui_event_MenuScreen_waterTempRaise,
                   &ui_MenuScreen_waterTempMinus, &ui_MenuScreen_waterTempValue, &ui_MenuScreen_waterTempPlus);

    // Row 4: STEAM TEMP stepper (gold accent on +). CAR-358.
    qs_stepper_row(ui_MenuScreen_contentPanel, &gm_ic_thermo, "STEAM TEMP", GM_GOLD,
                   ui_event_MenuScreen_steamTempLower, ui_event_MenuScreen_steamTempRaise,
                   &ui_MenuScreen_steamTempMinus, &ui_MenuScreen_steamTempValue, &ui_MenuScreen_steamTempPlus);

    // Row 5: SCALE toggle — HIDDEN (PRO-597). There is no persisted "BLE scale
    // enabled" runtime setting to bind to: the scale connects automatically by
    // controller mode (BLEScaleScanPolicy::shouldScanForBleScaleMode over
    // brew/grind/manual) and is only compiled in/out via the GAGGIMATE_ENABLE_
    // BLE_SCALE build flag — there is no user-facing enable/disable flag. Rather
    // than fabricate fake plumbing for a control that can't reflect real state,
    // the row is hidden until a genuine runtime toggle concept exists. The
    // switch object is still created + tracked (destroy/palette invariants) but
    // the row is flagged LV_OBJ_FLAG_HIDDEN so flex layout skips it, and no
    // value-changed handler is wired (no dead no-op callback).
    {
        lv_obj_t *row = qs_row(ui_MenuScreen_contentPanel, &gm_ic_scale, "SCALE");
        ui_MenuScreen_scaleSwitch = lv_switch_create(row);
        lv_obj_set_size(ui_MenuScreen_scaleSwitch, 56, 28);
        lv_obj_set_ext_click_area(ui_MenuScreen_scaleSwitch, 12);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    // Row 6: explicit confirmation is required; the dialog omits confirmation
    // when the conservative controller policy says restart is unsafe.
    {
        lv_obj_t *row = qs_row(ui_MenuScreen_contentPanel, &gm_ic_power, "RESTART");
        ui_MenuScreen_restartButton = lv_btn_create(row);
        lv_obj_set_size(ui_MenuScreen_restartButton, 112, 36);
        lv_obj_set_style_bg_color(ui_MenuScreen_restartButton, GM_RED, 0);
        lv_obj_set_style_bg_opa(ui_MenuScreen_restartButton, LV_OPA_COVER, 0);
        lv_obj_set_ext_click_area(ui_MenuScreen_restartButton, 12);
        lv_obj_add_event_cb(ui_MenuScreen_restartButton, ui_event_MenuScreen_restart, LV_EVENT_ALL, NULL);
        qs_restart_label = lv_label_create(ui_MenuScreen_restartButton);
        lv_label_set_text(qs_restart_label, "RESTART");
        lv_obj_set_style_text_font(qs_restart_label, &grotesk_16, 0);
        lv_obj_set_style_text_color(qs_restart_label, GM_CONTENT, 0);
        lv_obj_center(qs_restart_label);
    }

    // CAR-358: obvious full-width "DONE" exit pill at the bottom -> mode hub.
    // The small top-left back arrow is hard to find/hit on the round display;
    // this is the discoverable exit. Pinned below the scroll panel so it never
    // scrolls away.
    ui_MenuScreen_doneButton = lv_btn_create(ui_MenuScreen);
    lv_obj_remove_style_all(ui_MenuScreen_doneButton);
    lv_obj_set_size(ui_MenuScreen_doneButton, 240, 48);
    lv_obj_set_style_radius(ui_MenuScreen_doneButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_MenuScreen_doneButton, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui_MenuScreen_doneButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_MenuScreen_doneButton, 1, 0);
    lv_obj_set_style_border_color(ui_MenuScreen_doneButton, GM_CONTENT, 0);
    lv_obj_set_style_border_opa(ui_MenuScreen_doneButton, LV_OPA_40, 0);
    lv_obj_align(ui_MenuScreen_doneButton, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_ext_click_area(ui_MenuScreen_doneButton, 12);
    lv_obj_add_event_cb(ui_MenuScreen_doneButton, ui_event_MenuScreen_back, LV_EVENT_ALL, NULL);

    qs_done_label = lv_label_create(ui_MenuScreen_doneButton);
    lv_label_set_text(qs_done_label, "DONE");
    lv_obj_set_style_text_font(qs_done_label, &grotesk_16, 0);
    lv_obj_set_style_text_color(qs_done_label, GM_CONTENT, 0);
    lv_obj_set_style_text_letter_space(qs_done_label, GM_TRACK_KICKER, 0);
    lv_obj_center(qs_done_label);

    lv_obj_add_event_cb(ui_MenuScreen, ui_event_MenuScreen, LV_EVENT_ALL, NULL);
}

void ui_MenuScreen_screen_destroy(void) {
    if (ui_MenuScreen)
        lv_obj_del(ui_MenuScreen);

    ui_MenuScreen = NULL;
    ui_MenuScreen_contentPanel = NULL;
    ui_MenuScreen_backButton = NULL;
    ui_MenuScreen_doneButton = NULL;
    ui_MenuScreen_restartButton = NULL;
    ui_MenuScreen_brightnessSwitch = NULL;
    ui_MenuScreen_brewTempValue = NULL;
    ui_MenuScreen_brewTempMinus = NULL;
    ui_MenuScreen_brewTempPlus = NULL;
    ui_MenuScreen_waterTempValue = NULL;
    ui_MenuScreen_waterTempMinus = NULL;
    ui_MenuScreen_waterTempPlus = NULL;
    ui_MenuScreen_steamTempValue = NULL;
    ui_MenuScreen_steamTempMinus = NULL;
    ui_MenuScreen_steamTempPlus = NULL;
    ui_MenuScreen_scaleSwitch = NULL;

    qs_kicker = NULL;
    qs_done_label = NULL;
    qs_restart_label = NULL;
    qs_restart_dialog = NULL;
    for (int i = 0; i < QS_MAX_ROWS; i++) {
        qs_row_icons[i] = NULL;
        qs_row_labels[i] = NULL;
    }
    for (int i = 0; i < QS_MAX_STEPPERS; i++) {
        qs_stepper_minus_labels[i] = NULL;
        qs_stepper_plus_labels[i] = NULL;
        qs_stepper_plus_btns[i] = NULL;
    }
    qs_row_count = 0;
    qs_stepper_count = 0;
}

// CAR-279 review fix: recolor Quick-settings children with the active palette
// so the screen stays legible on UI_THEME_LIGHT (where styleScreenBase()
// repaints the bg from GM_BG black to a near-white surface).
//   text         - row labels + temp value readouts + stepper +/- glyphs + DONE
//   muted        - QUICK SETTINGS kicker
//   buttonSurface- minus button + DONE button background
//   accent       - plus button background
//
// CAR-358: now covers WATER/STEAM TEMP rows, all three stepper +/- glyph pairs,
// every temp value label, and the DONE button — each is a NEW themable widget
// that would otherwise vanish on the light theme.
void ui_MenuScreen_apply_palette(lv_color_t text, lv_color_t muted, lv_color_t buttonSurface, lv_color_t accent) {
    // `accent` no longer used: stepper "+" buttons keep their per-mode accent
    // (brew=red/water=blue/steam=gold) via qs_stepper_plus_accents. Kept in the
    // shared signature for the other screens' palette passes. CAR-358.
    (void)accent;
    if (qs_kicker != NULL && lv_obj_is_valid(qs_kicker)) {
        lv_obj_set_style_text_color(qs_kicker, muted, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    for (int i = 0; i < qs_row_count; i++) {
        if (qs_row_icons[i] != NULL && lv_obj_is_valid(qs_row_icons[i])) {
            lv_obj_set_style_img_recolor(qs_row_icons[i], text, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor_opa(qs_row_icons[i], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (qs_row_labels[i] != NULL && lv_obj_is_valid(qs_row_labels[i])) {
            lv_obj_set_style_text_color(qs_row_labels[i], text, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    // Temp value readouts (brew/water/steam).
    lv_obj_t *values[] = {ui_MenuScreen_brewTempValue, ui_MenuScreen_waterTempValue, ui_MenuScreen_steamTempValue};
    for (int i = 0; i < (int)(sizeof(values) / sizeof(values[0])); i++) {
        if (values[i] != NULL && lv_obj_is_valid(values[i])) {
            lv_obj_set_style_text_color(values[i], text, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    // Stepper "-" button surfaces + their glyph labels.
    lv_obj_t *minus_btns[] = {ui_MenuScreen_brewTempMinus, ui_MenuScreen_waterTempMinus, ui_MenuScreen_steamTempMinus};
    for (int i = 0; i < (int)(sizeof(minus_btns) / sizeof(minus_btns[0])); i++) {
        if (minus_btns[i] != NULL && lv_obj_is_valid(minus_btns[i])) {
            lv_obj_set_style_bg_color(minus_btns[i], buttonSurface, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    // Stepper "+" button surfaces — restore each row's PER-MODE accent
    // (brew=red, water=blue, steam=gold), NOT the single shared `accent`.
    // CAR-358 review (Codex P2): a shared accent here wiped the blue/gold
    // water/steam cues on every render.
    for (int i = 0; i < qs_stepper_count; i++) {
        if (qs_stepper_plus_btns[i] != NULL && lv_obj_is_valid(qs_stepper_plus_btns[i])) {
            lv_obj_set_style_bg_color(qs_stepper_plus_btns[i], qs_stepper_plus_accents[i],
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    // Stepper +/- glyph labels (all rows).
    for (int i = 0; i < qs_stepper_count; i++) {
        if (qs_stepper_minus_labels[i] != NULL && lv_obj_is_valid(qs_stepper_minus_labels[i])) {
            lv_obj_set_style_text_color(qs_stepper_minus_labels[i], text, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (qs_stepper_plus_labels[i] != NULL && lv_obj_is_valid(qs_stepper_plus_labels[i])) {
            lv_obj_set_style_text_color(qs_stepper_plus_labels[i], text, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    // DONE exit button (surface bg + text). CAR-358.
    if (ui_MenuScreen_doneButton != NULL && lv_obj_is_valid(ui_MenuScreen_doneButton)) {
        lv_obj_set_style_bg_color(ui_MenuScreen_doneButton, buttonSurface, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(ui_MenuScreen_doneButton, text, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (qs_done_label != NULL && lv_obj_is_valid(qs_done_label)) {
        lv_obj_set_style_text_color(qs_done_label, text, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_MenuScreen_restartButton != NULL && lv_obj_is_valid(ui_MenuScreen_restartButton)) {
        lv_obj_set_style_bg_color(ui_MenuScreen_restartButton, GM_RED, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (qs_restart_label != NULL && lv_obj_is_valid(qs_restart_label)) {
        lv_obj_set_style_text_color(qs_restart_label, text, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}
