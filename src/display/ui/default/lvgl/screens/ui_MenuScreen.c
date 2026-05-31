// CAR-279: ui_MenuScreen rebuilt as the Nothing-theme Quick-settings list.
// The mode-hub role moved to ui_ModeScreen (CAR-291). Layout: kicker on top,
// 3 settings rows (brightness toggle / brew-temp stepper / scale toggle),
// back button -> mode hub via onBackToModeScreen. Backend wiring for brightness +
// scale toggles is intentionally STUBBED for this PR (visual completeness
// per the design); follow-up will hook them to the real state.

#include "../ui.h"
#include "../gm_ui.h"

lv_obj_t *ui_MenuScreen = NULL;
lv_obj_t *ui_MenuScreen_contentPanel = NULL;
lv_obj_t *ui_MenuScreen_backButton = NULL;
lv_obj_t *ui_MenuScreen_brightnessSwitch = NULL;
lv_obj_t *ui_MenuScreen_brewTempValue = NULL;
lv_obj_t *ui_MenuScreen_brewTempMinus = NULL;
lv_obj_t *ui_MenuScreen_brewTempPlus = NULL;
lv_obj_t *ui_MenuScreen_scaleSwitch = NULL;

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

// CAR-279 TODO: wire to real backlight setting. For now the toggle is
// visual-only -- DefaultUI will not push state to it.
static void ui_event_MenuScreen_brightness(lv_event_t *e) { (void)e; }

// CAR-279 TODO: wire to BLEScales/scale-enabled state.
static void ui_event_MenuScreen_scale(lv_event_t *e) { (void)e; }

// Helper: build a settings row (icon + label + control)
static lv_obj_t *qs_row(lv_obj_t *parent, const lv_img_dsc_t *icon, const char *label) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
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
    return row;
}

void ui_MenuScreen_screen_init(void) {
    ui_MenuScreen = gm_make_screen();
    lv_obj_add_event_cb(ui_MenuScreen, scr_unloaded_delete_cb, LV_EVENT_SCREEN_UNLOADED, ui_MenuScreen_screen_destroy);

    // Kicker
    gm_kicker(ui_MenuScreen, "QUICK SETTINGS", GM_MUTED);

    // Back button (top-left round) -> mode hub
    ui_MenuScreen_backButton = lv_imgbtn_create(ui_MenuScreen);
    lv_imgbtn_set_src(ui_MenuScreen_backButton, LV_IMGBTN_STATE_RELEASED, NULL, &gm_ic_back, NULL);
    lv_obj_set_size(ui_MenuScreen_backButton, 44, 44);
    lv_obj_align(ui_MenuScreen_backButton, LV_ALIGN_TOP_LEFT, 28, 28);
    lv_obj_set_style_img_recolor(ui_MenuScreen_backButton, GM_CONTENT, 0);
    lv_obj_set_style_img_recolor_opa(ui_MenuScreen_backButton, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(ui_MenuScreen_backButton, ui_event_MenuScreen_back, LV_EVENT_ALL, NULL);

    // Settings list panel
    ui_MenuScreen_contentPanel = lv_obj_create(ui_MenuScreen);
    lv_obj_remove_style_all(ui_MenuScreen_contentPanel);
    lv_obj_set_size(ui_MenuScreen_contentPanel, 320, 220);
    lv_obj_align(ui_MenuScreen_contentPanel, LV_ALIGN_CENTER, 0, 24);
    lv_obj_set_flex_flow(ui_MenuScreen_contentPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_MenuScreen_contentPanel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ui_MenuScreen_contentPanel, 8, 0);
    lv_obj_clear_flag(ui_MenuScreen_contentPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_MenuScreen_contentPanel, LV_OBJ_FLAG_CLICKABLE);

    // Row 1: BRIGHTNESS toggle
    {
        lv_obj_t *row = qs_row(ui_MenuScreen_contentPanel, &gm_ic_sun, "BRIGHTNESS");
        ui_MenuScreen_brightnessSwitch = lv_switch_create(row);
        lv_obj_set_size(ui_MenuScreen_brightnessSwitch, 56, 28);
        lv_obj_set_ext_click_area(ui_MenuScreen_brightnessSwitch, 12);
        lv_obj_add_event_cb(ui_MenuScreen_brightnessSwitch, ui_event_MenuScreen_brightness, LV_EVENT_VALUE_CHANGED, NULL);
    }

    // Row 2: BREW TEMP stepper (-/+ around an ndot_28 value label)
    {
        lv_obj_t *row = qs_row(ui_MenuScreen_contentPanel, &gm_ic_thermo, "BREW TEMP");

        ui_MenuScreen_brewTempMinus = lv_btn_create(row);
        lv_obj_set_size(ui_MenuScreen_brewTempMinus, 38, 38);
        lv_obj_set_style_radius(ui_MenuScreen_brewTempMinus, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ui_MenuScreen_brewTempMinus, GM_SURFACE, 0);
        lv_obj_set_style_bg_opa(ui_MenuScreen_brewTempMinus, LV_OPA_COVER, 0);
        lv_obj_set_ext_click_area(ui_MenuScreen_brewTempMinus, 12);
        lv_obj_t *minusLab = lv_label_create(ui_MenuScreen_brewTempMinus);
        lv_label_set_text(minusLab, "-");
        lv_obj_set_style_text_font(minusLab, &ndot_28, 0);
        lv_obj_set_style_text_color(minusLab, GM_CONTENT, 0);
        lv_obj_center(minusLab);
        lv_obj_add_event_cb(ui_MenuScreen_brewTempMinus, ui_event_MenuScreen_brewTempLower, LV_EVENT_ALL, NULL);

        ui_MenuScreen_brewTempValue = lv_label_create(row);
        // Initial placeholder; overridden the moment the screen activates by
        // the targetTemp reactive effect in DefaultUI::setupReactive().
        lv_label_set_text(ui_MenuScreen_brewTempValue, "93");
        lv_obj_set_style_text_font(ui_MenuScreen_brewTempValue, &ndot_28, 0);
        lv_obj_set_style_text_color(ui_MenuScreen_brewTempValue, GM_CONTENT, 0);

        ui_MenuScreen_brewTempPlus = lv_btn_create(row);
        lv_obj_set_size(ui_MenuScreen_brewTempPlus, 38, 38);
        lv_obj_set_style_radius(ui_MenuScreen_brewTempPlus, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ui_MenuScreen_brewTempPlus, GM_RED, 0);
        lv_obj_set_style_bg_opa(ui_MenuScreen_brewTempPlus, LV_OPA_COVER, 0);
        lv_obj_set_ext_click_area(ui_MenuScreen_brewTempPlus, 12);
        lv_obj_t *plusLab = lv_label_create(ui_MenuScreen_brewTempPlus);
        lv_label_set_text(plusLab, "+");
        lv_obj_set_style_text_font(plusLab, &ndot_28, 0);
        lv_obj_set_style_text_color(plusLab, GM_CONTENT, 0);
        lv_obj_center(plusLab);
        lv_obj_add_event_cb(ui_MenuScreen_brewTempPlus, ui_event_MenuScreen_brewTempRaise, LV_EVENT_ALL, NULL);
    }

    // Row 3: SCALE toggle
    {
        lv_obj_t *row = qs_row(ui_MenuScreen_contentPanel, &gm_ic_scale, "SCALE");
        ui_MenuScreen_scaleSwitch = lv_switch_create(row);
        lv_obj_set_size(ui_MenuScreen_scaleSwitch, 56, 28);
        lv_obj_set_ext_click_area(ui_MenuScreen_scaleSwitch, 12);
        lv_obj_add_event_cb(ui_MenuScreen_scaleSwitch, ui_event_MenuScreen_scale, LV_EVENT_VALUE_CHANGED, NULL);
    }

    lv_obj_add_event_cb(ui_MenuScreen, ui_event_MenuScreen, LV_EVENT_ALL, NULL);
}

void ui_MenuScreen_screen_destroy(void) {
    if (ui_MenuScreen)
        lv_obj_del(ui_MenuScreen);

    ui_MenuScreen = NULL;
    ui_MenuScreen_contentPanel = NULL;
    ui_MenuScreen_backButton = NULL;
    ui_MenuScreen_brightnessSwitch = NULL;
    ui_MenuScreen_brewTempValue = NULL;
    ui_MenuScreen_brewTempMinus = NULL;
    ui_MenuScreen_brewTempPlus = NULL;
    ui_MenuScreen_scaleSwitch = NULL;
}
