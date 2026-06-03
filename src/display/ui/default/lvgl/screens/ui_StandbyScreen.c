// GaggiMate "Nothing" theme — Standby (ambient clock).
// Rewritten from the SquareLine export (CAR-277). Keeps the original
// object globals + event/destroy hooks so DefaultUI resolves unchanged,
// but lays them out per the design and stores live handles in gm_h.

#include "../ui.h"
#include "../gm_ui.h" // GM_* palette, gm_h handles, shared builders

lv_obj_t *ui_StandbyScreen = NULL;
lv_obj_t *ui_StandbyScreen_logo = NULL;            // kept (christmas / theme code), hidden
lv_obj_t *ui_StandbyScreen_time = NULL;            // hero clock (ndot_120)
lv_obj_t *ui_StandbyScreen_statusContainer = NULL; // top status bar
lv_obj_t *ui_StandbyScreen_wifiIcon = NULL;
lv_obj_t *ui_StandbyScreen_bluetoothIcon = NULL;
lv_obj_t *ui_StandbyScreen_updateIcon = NULL;      // update-available dot
lv_obj_t *ui_StandbyScreen_touchIcon = NULL;       // kept for ABI, unused/hidden
lv_obj_t *ui_StandbyScreen_mainLabel = NULL;       // kicker / state-message line

// event funtions
void ui_event_StandbyScreen(lv_event_t *e) {
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_CLICKED) {
        onWakeup(e);
    }
}

// Small recolored status icon: gm_ic_* masters are 40px. Widget is sized to
// target_px so the flex layout slot matches the rendered size; zoom maps the
// 40px source down to target_px.
static lv_obj_t *standby_status_icon(lv_obj_t *parent, const lv_img_dsc_t *src, int target_px) {
    lv_obj_t *im = lv_img_create(parent);
    lv_img_set_src(im, src);
    lv_obj_set_size(im, target_px, target_px);
    lv_img_set_zoom(im, (uint16_t)(256 * target_px / 40));
    lv_img_set_size_mode(im, LV_IMG_SIZE_MODE_REAL);
    lv_img_set_pivot(im, 0, 0);
    lv_obj_set_style_img_recolor(im, GM_MUTED, 0);
    lv_obj_set_style_img_recolor_opa(im, LV_OPA_COVER, 0);
    return im;
}

// build funtions

void ui_StandbyScreen_screen_init(void) {
    ui_StandbyScreen = gm_make_screen();
    lv_obj_add_event_cb(ui_StandbyScreen, scr_unloaded_delete_cb, LV_EVENT_SCREEN_UNLOADED, ui_StandbyScreen_screen_destroy);

    // ── Top status bar: wifi · bt · update dot ──
    // Container + update dot are lv_obj_create() (clickable by default). Clear
    // the flag so taps over the status row fall through to ui_StandbyScreen's
    // wake handler instead of being silently swallowed.
    ui_StandbyScreen_statusContainer = lv_obj_create(ui_StandbyScreen);
    lv_obj_remove_style_all(ui_StandbyScreen_statusContainer);
    lv_obj_set_size(ui_StandbyScreen_statusContainer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(ui_StandbyScreen_statusContainer, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_flex_flow(ui_StandbyScreen_statusContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_StandbyScreen_statusContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui_StandbyScreen_statusContainer, 16, 0);
    lv_obj_clear_flag(ui_StandbyScreen_statusContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_StandbyScreen_statusContainer, LV_OBJ_FLAG_CLICKABLE);

    ui_StandbyScreen_wifiIcon = standby_status_icon(ui_StandbyScreen_statusContainer, &gm_ic_wifi, 20);
    ui_StandbyScreen_bluetoothIcon = standby_status_icon(ui_StandbyScreen_statusContainer, &gm_ic_bt, 20);

    ui_StandbyScreen_updateIcon = lv_obj_create(ui_StandbyScreen_statusContainer);
    lv_obj_remove_style_all(ui_StandbyScreen_updateIcon);
    lv_obj_set_size(ui_StandbyScreen_updateIcon, 8, 8);
    lv_obj_set_style_radius(ui_StandbyScreen_updateIcon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_StandbyScreen_updateIcon, GM_GOLD, 0);
    lv_obj_set_style_bg_opa(ui_StandbyScreen_updateIcon, LV_OPA_COVER, 0);
    lv_obj_add_flag(ui_StandbyScreen_updateIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_StandbyScreen_updateIcon, LV_OBJ_FLAG_CLICKABLE);

    // ── Hero ambient clock ──
    ui_StandbyScreen_time = lv_label_create(ui_StandbyScreen);
    lv_label_set_recolor(ui_StandbyScreen_time, true);
    lv_label_set_text(ui_StandbyScreen_time, "--:--");
    lv_obj_set_style_text_font(ui_StandbyScreen_time, &ndot_120, 0);
    lv_obj_set_style_text_color(ui_StandbyScreen_time, GM_CONTENT, 0);
    lv_obj_align(ui_StandbyScreen_time, LV_ALIGN_CENTER, 0, -28);
    gm_h.clock = ui_StandbyScreen_time;

    // AM/PM indicator — ndot_120 only covers 0x30-0x3A, so the suffix is a
    // separate label in spacemono_14. Hidden when 24-hour format is active.
    gm_h.ampm = lv_label_create(ui_StandbyScreen);
    lv_label_set_text(gm_h.ampm, "");
    lv_obj_set_style_text_font(gm_h.ampm, &spacemono_14, 0);
    lv_obj_set_style_text_color(gm_h.ampm, GM_MUTED, 0);
    lv_obj_set_style_text_letter_space(gm_h.ampm, GM_TRACK_KICKER, 0);
    lv_obj_align(gm_h.ampm, LV_ALIGN_CENTER, 0, 36);
    lv_obj_add_flag(gm_h.ampm, LV_OBJ_FLAG_HIDDEN);

    // ── Kicker / state-message line (driven by the standby effect) ──
    ui_StandbyScreen_mainLabel = lv_label_create(ui_StandbyScreen);
    lv_label_set_text(ui_StandbyScreen_mainLabel, "STANDBY \xC2\xB7 READY");
    lv_obj_set_style_text_font(ui_StandbyScreen_mainLabel, &spacemono_14, 0);
    lv_obj_set_style_text_color(ui_StandbyScreen_mainLabel, GM_MUTED, 0);
    lv_obj_set_style_text_letter_space(ui_StandbyScreen_mainLabel, GM_TRACK_KICKER, 0);
    lv_obj_align(ui_StandbyScreen_mainLabel, LV_ALIGN_CENTER, 0, 56);
    gm_h.status_label = ui_StandbyScreen_mainLabel;

    // ── Temperature sub-line ──
    gm_h.standby_temp = lv_label_create(ui_StandbyScreen);
    lv_label_set_text(gm_h.standby_temp, "--");
    lv_obj_set_style_text_font(gm_h.standby_temp, &grotesk_16, 0);
    lv_obj_set_style_text_color(gm_h.standby_temp, GM_CONTENT, 0);
    lv_obj_align(gm_h.standby_temp, LV_ALIGN_CENTER, 0, 90);

    // ── Kept-but-hidden ABI objects (referenced by christmas/theme code) ──
    ui_StandbyScreen_logo = lv_img_create(ui_StandbyScreen);
    lv_img_set_src(ui_StandbyScreen_logo, &ui_img_logo_png);
    lv_obj_add_flag(ui_StandbyScreen_logo, LV_OBJ_FLAG_HIDDEN);

    // Repurposed: was an lv_img in the old SquareLine standby; now a label so the
    // chip-bar pattern can render its glyph. Do not call lv_img_* on this widget.
    ui_StandbyScreen_touchIcon = lv_label_create(ui_StandbyScreen);
    lv_label_set_text(ui_StandbyScreen_touchIcon, "");
    lv_obj_add_flag(ui_StandbyScreen_touchIcon, LV_OBJ_FLAG_HIDDEN);

    // ── Decorative mode chips (whole screen taps to wake) ──
    // Chips are lv_obj_create() objects (clickable by default). Clear the flag
    // so taps over the bar area fall through to ui_StandbyScreen's wake handler.
    lv_obj_t *chip_bar = gm_chip_bar(ui_StandbyScreen, 0, GM_CONTENT);
    lv_obj_clear_flag(chip_bar, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < 4; i++) {
        lv_obj_clear_flag(gm_h.chips[i], LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_add_event_cb(ui_StandbyScreen, ui_event_StandbyScreen, LV_EVENT_ALL, NULL);
}

void ui_StandbyScreen_screen_destroy(void) {
    if (ui_StandbyScreen)
        lv_obj_del(ui_StandbyScreen);

    // NULL screen variables
    ui_StandbyScreen = NULL;
    ui_StandbyScreen_logo = NULL;
    ui_StandbyScreen_time = NULL;
    ui_StandbyScreen_statusContainer = NULL;
    ui_StandbyScreen_wifiIcon = NULL;
    ui_StandbyScreen_bluetoothIcon = NULL;
    ui_StandbyScreen_updateIcon = NULL;
    ui_StandbyScreen_touchIcon = NULL;
    ui_StandbyScreen_mainLabel = NULL;
    gm_h.clock = NULL;
    gm_h.ampm = NULL;
    gm_h.standby_temp = NULL;
    gm_h.status_label = NULL;
    // StandbyScreen does NOT own gm_h.status_time — it builds its own status
    // bar manually without calling gm_status_bar(), so the global belongs to
    // whichever previous screen owned it (or to the next screen, whose init
    // runs before this destroy hook). Don't touch it here. CAR-297.
    for (int i = 0; i < 4; i++) {
        gm_h.chips[i] = NULL;
    }
}
