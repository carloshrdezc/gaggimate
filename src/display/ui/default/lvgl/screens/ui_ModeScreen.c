// GaggiMate "Nothing" theme - Mode launcher screen (CAR-291).
// Hand-written (not SquareLine). 4 mode tiles + standby + settings entry.
// Replaces ui_MenuScreen's old role as the mode hub. The Nothing-theme
// styling/layout (palette, sizes, alignments) is applied from
// DefaultUI::applyScreenPalette() via the styleMenuTile/styleRoundIconButton
// helpers; this file only builds the object tree and wires events.

#include "../ui.h"
#include "../gm_ui.h" // GM_* palette, gm_h handles, status-bar builder

lv_obj_t *uic_ModeScreen_dials_tempText;
lv_obj_t *uic_ModeScreen_dials_pressureText;
lv_obj_t *uic_ModeScreen_dials_pressureTarget;
lv_obj_t *uic_ModeScreen_dials_pressureGauge;
lv_obj_t *uic_ModeScreen_dials_tempTarget;
lv_obj_t *uic_ModeScreen_dials_tempGauge;

lv_obj_t *ui_ModeScreen = NULL;
lv_obj_t *ui_ModeScreen_dials = NULL;
lv_obj_t *ui_ModeScreen_contentPanel1 = NULL;
lv_obj_t *ui_ModeScreen_btnBrew = NULL;
lv_obj_t *ui_ModeScreen_btnSteam = NULL;
lv_obj_t *ui_ModeScreen_waterBtn = NULL;
lv_obj_t *ui_ModeScreen_grindBtn = NULL;
lv_obj_t *ui_ModeScreen_standbyButton = NULL;
lv_obj_t *ui_ModeScreen_settingsButton = NULL;

// -- tile event wrappers (LV_EVENT_ALL gate -> CLICKED handlers) --
static void ui_event_ModeScreen_btnBrew(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onBrewScreen(e);
}
static void ui_event_ModeScreen_btnSteam(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onSteamScreen(e);
}
static void ui_event_ModeScreen_waterBtn(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onWaterScreen(e);
}
static void ui_event_ModeScreen_grindBtn(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onGrindScreen(e);
}
static void ui_event_ModeScreen_standbyButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onStandby(e);
}
static void ui_event_ModeScreen_settingsButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onSettingsClick(e);
}

void ui_event_ModeScreen(lv_event_t *e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_SCREEN_LOADED) {
        onModeScreenLoad(e);
    }
}

// build functions

void ui_ModeScreen_screen_init(void) {
    ui_ModeScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ModeScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ui_ModeScreen, scr_unloaded_delete_cb, LV_EVENT_SCREEN_UNLOADED, ui_ModeScreen_screen_destroy);
    ui_object_set_themeable_style_property(ui_ModeScreen, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR,
                                           _ui_theme_color_Dark);
    ui_object_set_themeable_style_property(ui_ModeScreen, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_Dark);

    ui_ModeScreen_dials = ui_dials_create(ui_ModeScreen);
    lv_obj_set_x(ui_ModeScreen_dials, 0);
    lv_obj_set_y(ui_ModeScreen_dials, 0);

    ui_ModeScreen_standbyButton = lv_imgbtn_create(ui_ModeScreen);
    lv_imgbtn_set_src(ui_ModeScreen_standbyButton, LV_IMGBTN_STATE_RELEASED, NULL, &gm_ic_power, NULL);
    lv_obj_set_width(ui_ModeScreen_standbyButton, 40);
    lv_obj_set_height(ui_ModeScreen_standbyButton, 40);
    lv_obj_set_x(ui_ModeScreen_standbyButton, 0);
    lv_obj_set_y(ui_ModeScreen_standbyButton, 210);
    lv_obj_set_align(ui_ModeScreen_standbyButton, LV_ALIGN_CENTER);
    ui_object_set_themeable_style_property(ui_ModeScreen_standbyButton, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_IMG_RECOLOR,
                                           _ui_theme_color_NiceWhite);
    ui_object_set_themeable_style_property(ui_ModeScreen_standbyButton, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_IMG_RECOLOR_OPA,
                                           _ui_theme_alpha_NiceWhite);

    // CAR-291: settings entry (top-right round icon). Uses the existing
    // SquareLine settings asset; round-button styling is applied from
    // DefaultUI::applyScreenPalette() so it tracks the palette.
    ui_ModeScreen_settingsButton = lv_imgbtn_create(ui_ModeScreen);
    lv_imgbtn_set_src(ui_ModeScreen_settingsButton, LV_IMGBTN_STATE_RELEASED, NULL, &ui_img_340148213, NULL);
    lv_obj_set_width(ui_ModeScreen_settingsButton, 40);
    lv_obj_set_height(ui_ModeScreen_settingsButton, 40);
    lv_obj_set_align(ui_ModeScreen_settingsButton, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_x(ui_ModeScreen_settingsButton, -28);
    lv_obj_set_y(ui_ModeScreen_settingsButton, 28);
    ui_object_set_themeable_style_property(ui_ModeScreen_settingsButton, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_IMG_RECOLOR,
                                           _ui_theme_color_NiceWhite);
    ui_object_set_themeable_style_property(ui_ModeScreen_settingsButton, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_IMG_RECOLOR_OPA,
                                           _ui_theme_alpha_NiceWhite);

    ui_ModeScreen_contentPanel1 = lv_obj_create(ui_ModeScreen);
    lv_obj_set_width(ui_ModeScreen_contentPanel1, 360);
    lv_obj_set_height(ui_ModeScreen_contentPanel1, 360);
    lv_obj_set_align(ui_ModeScreen_contentPanel1, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(ui_ModeScreen_contentPanel1, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ui_ModeScreen_contentPanel1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_obj_clear_flag(ui_ModeScreen_contentPanel1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_ModeScreen_contentPanel1, 180, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_ModeScreen_contentPanel1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ModeScreen_contentPanel1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_ModeScreen_contentPanel1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_ModeScreen_contentPanel1, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_ModeScreen_contentPanel1, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_ModeScreen_contentPanel1, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_ModeScreen_contentPanel1, 30, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_ModeScreen_btnBrew = lv_btn_create(ui_ModeScreen_contentPanel1);
    lv_obj_set_width(ui_ModeScreen_btnBrew, 120);
    lv_obj_set_height(ui_ModeScreen_btnBrew, 120);
    lv_obj_set_align(ui_ModeScreen_btnBrew, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_ModeScreen_btnBrew, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_ModeScreen_btnBrew, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_ModeScreen_btnBrew, lv_color_hex(0xFAFAFA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ModeScreen_btnBrew, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(ui_ModeScreen_btnBrew, &gm_ic_cup, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(ui_ModeScreen_btnBrew, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_IMG_RECOLOR,
                                           _ui_theme_color_NiceWhite);
    ui_object_set_themeable_style_property(ui_ModeScreen_btnBrew, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_IMG_RECOLOR_OPA,
                                           _ui_theme_alpha_NiceWhite);
    ui_object_set_themeable_style_property(ui_ModeScreen_btnBrew, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_SHADOW_COLOR,
                                           _ui_theme_color_Dark);
    ui_object_set_themeable_style_property(ui_ModeScreen_btnBrew, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_SHADOW_OPA,
                                           _ui_theme_alpha_Dark);
    lv_obj_set_style_shadow_width(ui_ModeScreen_btnBrew, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_ModeScreen_btnBrew, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_ModeScreen_btnSteam = lv_btn_create(ui_ModeScreen_contentPanel1);
    lv_obj_set_width(ui_ModeScreen_btnSteam, 120);
    lv_obj_set_height(ui_ModeScreen_btnSteam, 120);
    lv_obj_set_align(ui_ModeScreen_btnSteam, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_ModeScreen_btnSteam, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_ModeScreen_btnSteam, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_ModeScreen_btnSteam, lv_color_hex(0xFAFAFA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ModeScreen_btnSteam, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(ui_ModeScreen_btnSteam, &gm_ic_steam, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(ui_ModeScreen_btnSteam, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_IMG_RECOLOR,
                                           _ui_theme_color_NiceWhite);
    ui_object_set_themeable_style_property(ui_ModeScreen_btnSteam, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_IMG_RECOLOR_OPA,
                                           _ui_theme_alpha_NiceWhite);
    ui_object_set_themeable_style_property(ui_ModeScreen_btnSteam, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_SHADOW_COLOR,
                                           _ui_theme_color_Dark);
    ui_object_set_themeable_style_property(ui_ModeScreen_btnSteam, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_SHADOW_OPA,
                                           _ui_theme_alpha_Dark);
    lv_obj_set_style_shadow_width(ui_ModeScreen_btnSteam, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_ModeScreen_btnSteam, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_ModeScreen_waterBtn = lv_btn_create(ui_ModeScreen_contentPanel1);
    lv_obj_set_width(ui_ModeScreen_waterBtn, 120);
    lv_obj_set_height(ui_ModeScreen_waterBtn, 120);
    lv_obj_set_align(ui_ModeScreen_waterBtn, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_ModeScreen_waterBtn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_ModeScreen_waterBtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_ModeScreen_waterBtn, lv_color_hex(0xFAFAFA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ModeScreen_waterBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(ui_ModeScreen_waterBtn, &gm_ic_drop, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(ui_ModeScreen_waterBtn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_IMG_RECOLOR,
                                           _ui_theme_color_NiceWhite);
    ui_object_set_themeable_style_property(ui_ModeScreen_waterBtn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_IMG_RECOLOR_OPA,
                                           _ui_theme_alpha_NiceWhite);
    ui_object_set_themeable_style_property(ui_ModeScreen_waterBtn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_SHADOW_COLOR,
                                           _ui_theme_color_Dark);
    ui_object_set_themeable_style_property(ui_ModeScreen_waterBtn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_SHADOW_OPA,
                                           _ui_theme_alpha_Dark);
    lv_obj_set_style_shadow_width(ui_ModeScreen_waterBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_ModeScreen_waterBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_ModeScreen_grindBtn = lv_btn_create(ui_ModeScreen_contentPanel1);
    lv_obj_set_width(ui_ModeScreen_grindBtn, 120);
    lv_obj_set_height(ui_ModeScreen_grindBtn, 120);
    lv_obj_set_align(ui_ModeScreen_grindBtn, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_ModeScreen_grindBtn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_ModeScreen_grindBtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_ModeScreen_grindBtn, lv_color_hex(0xFAFAFA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ModeScreen_grindBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(ui_ModeScreen_grindBtn, &ui_img_363557387, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(ui_ModeScreen_grindBtn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_IMG_RECOLOR,
                                           _ui_theme_color_NiceWhite);
    ui_object_set_themeable_style_property(ui_ModeScreen_grindBtn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_IMG_RECOLOR_OPA,
                                           _ui_theme_alpha_NiceWhite);
    ui_object_set_themeable_style_property(ui_ModeScreen_grindBtn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_SHADOW_COLOR,
                                           _ui_theme_color_Dark);
    ui_object_set_themeable_style_property(ui_ModeScreen_grindBtn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_SHADOW_OPA,
                                           _ui_theme_alpha_Dark);
    lv_obj_set_style_shadow_width(ui_ModeScreen_grindBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_ModeScreen_grindBtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_ModeScreen_standbyButton, ui_event_ModeScreen_standbyButton, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ModeScreen_settingsButton, ui_event_ModeScreen_settingsButton, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ModeScreen_btnBrew, ui_event_ModeScreen_btnBrew, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ModeScreen_btnSteam, ui_event_ModeScreen_btnSteam, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ModeScreen_waterBtn, ui_event_ModeScreen_waterBtn, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ModeScreen_grindBtn, ui_event_ModeScreen_grindBtn, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ModeScreen, ui_event_ModeScreen, LV_EVENT_ALL, NULL);

    uic_ModeScreen_dials_tempGauge = ui_comp_get_child(ui_ModeScreen_dials, UI_COMP_DIALS_TEMPGAUGE);
    uic_ModeScreen_dials_tempTarget = ui_comp_get_child(ui_ModeScreen_dials, UI_COMP_DIALS_TEMPTARGET);
    uic_ModeScreen_dials_pressureGauge = ui_comp_get_child(ui_ModeScreen_dials, UI_COMP_DIALS_PRESSUREGAUGE);
    uic_ModeScreen_dials_pressureTarget = ui_comp_get_child(ui_ModeScreen_dials, UI_COMP_DIALS_PRESSURETARGET);
    uic_ModeScreen_dials_pressureText = ui_comp_get_child(ui_ModeScreen_dials, UI_COMP_DIALS_PRESSURETEXT);
    uic_ModeScreen_dials_tempText = ui_comp_get_child(ui_ModeScreen_dials, UI_COMP_DIALS_TEMPTEXT);
}

void ui_ModeScreen_screen_destroy(void) {
    if (ui_ModeScreen)
        lv_obj_del(ui_ModeScreen);

    ui_ModeScreen = NULL;
    ui_ModeScreen_dials = NULL;
    uic_ModeScreen_dials_tempGauge = NULL;
    uic_ModeScreen_dials_tempTarget = NULL;
    uic_ModeScreen_dials_pressureGauge = NULL;
    uic_ModeScreen_dials_pressureTarget = NULL;
    uic_ModeScreen_dials_pressureText = NULL;
    uic_ModeScreen_dials_tempText = NULL;
    ui_ModeScreen_standbyButton = NULL;
    ui_ModeScreen_settingsButton = NULL;
    ui_ModeScreen_contentPanel1 = NULL;
    ui_ModeScreen_btnBrew = NULL;
    ui_ModeScreen_btnSteam = NULL;
    ui_ModeScreen_waterBtn = NULL;
    ui_ModeScreen_grindBtn = NULL;
}
