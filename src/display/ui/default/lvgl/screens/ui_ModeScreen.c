// GaggiMate "Nothing" theme — Mode launcher screen (CAR-308).
// 2×2 mode-tile grid (Brew/Steam/Water/Grind) + bottom Standby pill +
// top-right Settings round icon. Replaces the old SquareLine-themed mode
// hub. The screen owns its visuals via the gm_* builders + GM_* tokens;
// DefaultUI no longer restyles it from applyScreenPalette().

#include "../ui.h"
#include "../gm_ui.h" // GM_* palette, gm_h handles, shared builders

lv_obj_t *ui_ModeScreen = NULL;
lv_obj_t *ui_ModeScreen_contentPanel1 = NULL;
lv_obj_t *ui_ModeScreen_btnBrew = NULL;
lv_obj_t *ui_ModeScreen_btnSteam = NULL;
lv_obj_t *ui_ModeScreen_waterBtn = NULL;
lv_obj_t *ui_ModeScreen_grindBtn = NULL;
lv_obj_t *ui_ModeScreen_standbyButton = NULL;
lv_obj_t *ui_ModeScreen_settingsButton = NULL;

// Snapshot of the gm_h.status_time label this screen owns. Used by the
// destroy hook to tell whether gm_h.status_time still points at our own
// clock vs. having been overwritten by the next screen's init. Mirrors
// CAR-294 / CAR-297 guarded-clobber pattern in ui_GrindScreen / ui_StatusScreen.
static lv_obj_t *ms_status_time = NULL;

// ── tile event wrappers (LV_EVENT_ALL gate -> CLICKED handlers) ──
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

// ── helpers ──

// Build a single mode tile: 124×124 rounded square with a centered icon
// and a bottom-aligned mono uppercase label. The accent color drives the
// icon recolor; bg/border are the inactive palette (white-on-black at low
// opacity). The "active" variant is reserved for a focused-mode highlight
// — we don't currently track focus on the launcher, so all four tiles are
// rendered inactive (the design's --active state is a focus indicator,
// not a permanent style).
static lv_obj_t *build_mode_tile(lv_obj_t *parent, const lv_img_dsc_t *icon_src,
                                 const char *label_txt, lv_color_t accent) {
    lv_obj_t *tile = lv_btn_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, 124, 124);
    lv_obj_set_style_radius(tile, 30, 0);
    // Inactive tile bg: white at ~10% (closest LVGL alpha step to the design's
    // 2.5% rgba(255,255,255,0.025) — bumped up so the tile reads on the OLED
    // panel where pure black soaks alpha; still subtle on the round bezel).
    lv_obj_set_style_bg_color(tile, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_10, 0);
    lv_obj_set_style_border_color(tile, GM_FAINT, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);

    // Icon: centered, slightly above center to leave room for the label.
    lv_obj_t *ic = lv_img_create(tile);
    lv_img_set_src(ic, icon_src);
    lv_obj_set_style_img_recolor(ic, accent, 0);
    lv_obj_set_style_img_recolor_opa(ic, LV_OPA_COVER, 0);
    lv_img_set_size_mode(ic, LV_IMG_SIZE_MODE_REAL);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -12);

    // Label: mono 12px (spacemono_14 is the smallest mono we ship), tracked,
    // muted; pinned to the bottom inside the tile.
    lv_obj_t *lbl = lv_label_create(tile);
    lv_label_set_text(lbl, label_txt);
    lv_obj_set_style_text_font(lbl, &spacemono_14, 0);
    lv_obj_set_style_text_color(lbl, GM_MUTED, 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -16);

    return tile;
}

// build functions

void ui_ModeScreen_screen_init(void) {
    ui_ModeScreen = gm_make_screen();
    lv_obj_add_event_cb(ui_ModeScreen, scr_unloaded_delete_cb, LV_EVENT_SCREEN_UNLOADED, ui_ModeScreen_screen_destroy);

    // ── Top status bar (wifi · bt · clock; no live dot — the launcher is
    // a static hub, not a live shot screen). ──
    gm_status_bar(ui_ModeScreen, false);
    ms_status_time = gm_h.status_time;

    // ── Settings round icon (top-right corner). Sits inside the round
    // 480px bezel safe-zone. ──
    ui_ModeScreen_settingsButton = lv_imgbtn_create(ui_ModeScreen);
    lv_imgbtn_set_src(ui_ModeScreen_settingsButton, LV_IMGBTN_STATE_RELEASED, NULL, &ui_img_340148213, NULL);
    lv_obj_set_size(ui_ModeScreen_settingsButton, 40, 40);
    lv_obj_align(ui_ModeScreen_settingsButton, LV_ALIGN_TOP_RIGHT, -56, 56);
    lv_obj_set_style_img_recolor(ui_ModeScreen_settingsButton, GM_CONTENT, 0);
    lv_obj_set_style_img_recolor_opa(ui_ModeScreen_settingsButton, LV_OPA_COVER, 0);

    // ── "SELECT MODE" kicker ──
    lv_obj_t *kicker = gm_kicker(ui_ModeScreen, "SELECT MODE", GM_MUTED);
    lv_obj_align(kicker, LV_ALIGN_TOP_MID, 0, 88);

    // ── 2×2 grid container ──
    // Borderless, transparent — the tiles do all the visible work. Sized
    // to (2*124 + 14) = 262 wide, (2*124 + 14) = 262 tall. Centered with
    // a small upward nudge so the bottom Standby pill has clear space.
    ui_ModeScreen_contentPanel1 = lv_obj_create(ui_ModeScreen);
    lv_obj_remove_style_all(ui_ModeScreen_contentPanel1);
    lv_obj_set_size(ui_ModeScreen_contentPanel1, 262, 262);
    lv_obj_align(ui_ModeScreen_contentPanel1, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_layout(ui_ModeScreen_contentPanel1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui_ModeScreen_contentPanel1, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ui_ModeScreen_contentPanel1, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_set_style_pad_row(ui_ModeScreen_contentPanel1, 14, 0);
    lv_obj_set_style_pad_column(ui_ModeScreen_contentPanel1, 14, 0);
    lv_obj_clear_flag(ui_ModeScreen_contentPanel1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_ModeScreen_contentPanel1, LV_OBJ_FLAG_CLICKABLE);

    // ── Mode tiles (insertion order = grid order: Brew, Steam, Water, Grind) ──
    ui_ModeScreen_btnBrew = build_mode_tile(ui_ModeScreen_contentPanel1, &gm_ic_cup, "BREW", GM_RED);
    ui_ModeScreen_btnSteam = build_mode_tile(ui_ModeScreen_contentPanel1, &gm_ic_steam, "STEAM", GM_GOLD);
    ui_ModeScreen_waterBtn = build_mode_tile(ui_ModeScreen_contentPanel1, &gm_ic_drop, "WATER", GM_BLUE);
    // Grind uses the existing coffee-bean PNG asset (no gm_ic_* equivalent yet).
    ui_ModeScreen_grindBtn = build_mode_tile(ui_ModeScreen_contentPanel1, &ui_img_363557387, "GRIND", GM_CONTENT);

    // ── Standby pill at the bottom ──
    // Rounded-rect ~38px tall, contains a power icon + "STANDBY" label.
    // Replaces the legacy 40×40 round button while preserving the
    // ui_ModeScreen_standbyButton symbol (DefaultUI binds onStandby to it).
    ui_ModeScreen_standbyButton = lv_btn_create(ui_ModeScreen);
    lv_obj_remove_style_all(ui_ModeScreen_standbyButton);
    lv_obj_set_size(ui_ModeScreen_standbyButton, LV_SIZE_CONTENT, 38);
    lv_obj_align(ui_ModeScreen_standbyButton, LV_ALIGN_BOTTOM_MID, 0, -46);
    lv_obj_set_style_radius(ui_ModeScreen_standbyButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_ModeScreen_standbyButton, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ui_ModeScreen_standbyButton, LV_OPA_10, 0);
    lv_obj_set_style_border_color(ui_ModeScreen_standbyButton, GM_FAINT, 0);
    lv_obj_set_style_border_width(ui_ModeScreen_standbyButton, 1, 0);
    lv_obj_set_style_border_opa(ui_ModeScreen_standbyButton, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(ui_ModeScreen_standbyButton, 0, 0);
    lv_obj_set_style_pad_left(ui_ModeScreen_standbyButton, 18, 0);
    lv_obj_set_style_pad_right(ui_ModeScreen_standbyButton, 20, 0);
    lv_obj_set_style_pad_top(ui_ModeScreen_standbyButton, 0, 0);
    lv_obj_set_style_pad_bottom(ui_ModeScreen_standbyButton, 0, 0);
    lv_obj_set_layout(ui_ModeScreen_standbyButton, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui_ModeScreen_standbyButton, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_ModeScreen_standbyButton, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui_ModeScreen_standbyButton, 9, 0);
    lv_obj_clear_flag(ui_ModeScreen_standbyButton, LV_OBJ_FLAG_SCROLLABLE);

    // CAR-308 P2 follow-up (Codex review): drop explicit lv_obj_set_size
    // and let REAL mode self-size the widget to the zoomed draw area —
    // matches the gm_icon() idiom in gm_ui.cpp:43-53. The earlier
    // explicit set_size fought LVGL's transformed self-size and could
    // make the power glyph clip or disappear.
    lv_obj_t *pwr = lv_img_create(ui_ModeScreen_standbyButton);
    lv_img_set_src(pwr, &gm_ic_power);                       // 40px native; pivot defaults to (20,20)
    lv_img_set_zoom(pwr, (uint16_t)(256 * 14 / 40));         // map 40 → 14px
    lv_img_set_size_mode(pwr, LV_IMG_SIZE_MODE_REAL);        // self-size = transformed size
    lv_obj_set_style_img_recolor(pwr, GM_MUTED, 0);
    lv_obj_set_style_img_recolor_opa(pwr, LV_OPA_COVER, 0);

    lv_obj_t *pwr_lbl = lv_label_create(ui_ModeScreen_standbyButton);
    lv_label_set_text(pwr_lbl, "STANDBY");
    lv_obj_set_style_text_font(pwr_lbl, &spacemono_14, 0);
    lv_obj_set_style_text_color(pwr_lbl, GM_MUTED, 0);
    lv_obj_set_style_text_letter_space(pwr_lbl, GM_TRACK_KICKER, 0);

    // ── event wiring ──
    lv_obj_add_event_cb(ui_ModeScreen_standbyButton, ui_event_ModeScreen_standbyButton, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ModeScreen_settingsButton, ui_event_ModeScreen_settingsButton, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ModeScreen_btnBrew, ui_event_ModeScreen_btnBrew, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ModeScreen_btnSteam, ui_event_ModeScreen_btnSteam, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ModeScreen_waterBtn, ui_event_ModeScreen_waterBtn, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ModeScreen_grindBtn, ui_event_ModeScreen_grindBtn, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ModeScreen, ui_event_ModeScreen, LV_EVENT_ALL, NULL);
}

void ui_ModeScreen_screen_destroy(void) {
    if (ui_ModeScreen)
        lv_obj_del(ui_ModeScreen);

    ui_ModeScreen = NULL;
    ui_ModeScreen_standbyButton = NULL;
    ui_ModeScreen_settingsButton = NULL;
    ui_ModeScreen_contentPanel1 = NULL;
    ui_ModeScreen_btnBrew = NULL;
    ui_ModeScreen_btnSteam = NULL;
    ui_ModeScreen_waterBtn = NULL;
    ui_ModeScreen_grindBtn = NULL;
    // Guarded-clobber for gm_h.status_time (CAR-294 / CAR-297). The next
    // screen's init runs before this destroy, so the global may already
    // point at the next screen's clock — only NULL it if it still refers
    // to ours.
    if (gm_h.status_time == ms_status_time) {
        gm_h.status_time = NULL;
    }
    ms_status_time = NULL;
}
