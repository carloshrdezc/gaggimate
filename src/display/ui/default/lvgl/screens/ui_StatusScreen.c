// GaggiMate "Nothing" theme — Status screen (brew · steam · water).
// Rebuilt from the SquareLine export (CAR-278). The same screen serves
// all three brewing modes; gm_status_apply_mode() retints + relayouts.
//
// The screen owns its objects via the `gm_h` live-update handles defined
// in gm_ui.h; the only globals kept are `ui_StatusScreen` (DefaultUI
// compares against it via `lv_scr_act()`) and the standard
// `_screen_init` / `_screen_destroy` symbols + `ui_event_StatusScreen`
// gesture handler that `DefaultUI::changeScreen()` resolves through the
// generated header. All other SquareLine globals are gone — see the
// updated DefaultUI for the new wiring.

#include "../ui.h"
#include "../gm_ui.h" // GM_* palette, gm_h handles, shared builders

lv_obj_t *ui_StatusScreen = NULL;

// Local snapshot of the clock label this screen owns. Captured from
// gm_h.status_time immediately after gm_status_bar() builds it. Used by the
// destroy hook to decide whether the global still belongs to *this* screen
// (see screen_destroy below for the full rationale). CAR-297 (mirrors the
// CAR-294 GrindScreen guarded-clobber fix).
static lv_obj_t *ss_status_time = NULL;

// event functions
void ui_event_StatusScreen(lv_event_t *e) {
    lv_event_code_t event_code = lv_event_get_code(e);

    // CAR-278: cancel during brew is reached via top-edge swipe -> menu, by design.
    // See onStatusScreenLoad in ui_events.cpp for rationale before adding an inline cancel chip.
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP) {
        lv_indev_wait_release(lv_indev_get_act());
        onMenuClick(e);
    }
    if (event_code == LV_EVENT_SCREEN_LOADED) {
        onStatusScreenLoad(e);
    }
    // CAR-292: tap-to-toggle for water dispense. The handler short-circuits in
    // every other mode, so this is safe while ui_StatusScreen is also hosting
    // brew + steam — neither one should react to a body tap.
    if (event_code == LV_EVENT_CLICKED) {
        onStatusScreenTap(e);
    }
}

// Chip-bar tap handlers — each chip navigates to its target screen.
static void status_chip0_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onMenuClick(e);
}
static void status_chip1_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onBrewScreen(e);
}
static void status_chip2_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onSteamScreen(e);
}
static void status_chip3_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onWaterScreen(e);
}

// build functions

void ui_StatusScreen_screen_init(void) {
    ui_StatusScreen = gm_make_screen();
    lv_obj_add_event_cb(ui_StatusScreen, scr_unloaded_delete_cb, LV_EVENT_SCREEN_UNLOADED, ui_StatusScreen_screen_destroy);

    // ── Top status bar (wifi · bt · clock + live dot) ──
    gm_status_bar(ui_StatusScreen, true);
    // Snapshot the clock label gm_status_bar() just stored in the global, so
    // the destroy hook can tell whether gm_h.status_time still points at our
    // own clock (vs. having been overwritten by the next screen's init).
    // CAR-297 (mirrors CAR-294 GrindScreen guarded-clobber).
    ss_status_time = gm_h.status_time;

    // ── Edge arc (270° sweep) hugging the bezel ──
    gm_h.arc = gm_edge_arc(ui_StatusScreen, GM_RED);

    // ── Kicker label above the hero ──
    gm_h.kicker = gm_kicker(ui_StatusScreen, "BREW", GM_RED);
    lv_obj_align(gm_h.kicker, LV_ALIGN_CENTER, 0, -84);

    // ── Hero numeral (timer / temp / weight) ──
    gm_h.hero = lv_label_create(ui_StatusScreen);
    lv_label_set_text(gm_h.hero, "0:00");
    lv_obj_set_style_text_font(gm_h.hero, &ndot_150, 0);
    lv_obj_set_style_text_color(gm_h.hero, GM_CONTENT, 0);
    // Shift left by half the unit-suffix width so hero+unit read as a
    // visually-centered group (hero_unit aligns OUT_RIGHT_BOTTOM of hero).
    lv_obj_align(gm_h.hero, LV_ALIGN_CENTER, -16, -8);

    // Hero unit suffix — accent-colored, sits to the right of the hero.
    gm_h.hero_unit = lv_label_create(ui_StatusScreen);
    lv_label_set_text(gm_h.hero_unit, "s");
    lv_obj_set_style_text_font(gm_h.hero_unit, &ndot_60, 0);
    lv_obj_set_style_text_color(gm_h.hero_unit, GM_RED, 0);
    lv_obj_align_to(gm_h.hero_unit, gm_h.hero, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -10);

    // ── Metric row (brew: WEIGHT · TEMP · PRESS · FLOW) ──
    lv_obj_t *row = lv_obj_create(ui_StatusScreen);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, 96);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    gm_h.m_weight = gm_metric(row, "WEIGHT", "0.0g", GM_CONTENT);
    gm_h.m_temp   = gm_metric(row, "TEMP",   "0\xC2\xB0", GM_CONTENT);
    gm_h.m_press  = gm_metric(row, "PRESS",  "0.0",  GM_CONTENT);
    gm_h.m_flow   = gm_metric(row, "FLOW",   "--",   GM_CONTENT);

    // Water-mode metrics share the same row container so they layout in
    // the same band when the brew metrics are hidden. Started hidden.
    gm_h.w_target = gm_metric(row, "TARGET", "0g",  GM_CONTENT);
    gm_h.w_temp   = gm_metric(row, "TEMP",   "0\xC2\xB0", GM_CONTENT);
    gm_h.w_flow   = gm_metric(row, "FLOW",   "--",  GM_CONTENT);
    lv_obj_add_flag(lv_obj_get_parent(gm_h.w_target), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lv_obj_get_parent(gm_h.w_temp),   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lv_obj_get_parent(gm_h.w_flow),   LV_OBJ_FLAG_HIDDEN);

    // ── Linear progress bar (water mode dispenser) ──
    gm_h.bar = gm_progress(ui_StatusScreen, GM_RED);
    lv_obj_align(gm_h.bar, LV_ALIGN_CENTER, 0, 140);
    lv_obj_add_flag(gm_h.bar, LV_OBJ_FLAG_HIDDEN);

    // ── READY pill (steam mode at-target) ──
    gm_h.pill = lv_obj_create(ui_StatusScreen);
    lv_obj_remove_style_all(gm_h.pill);
    lv_obj_set_size(gm_h.pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(gm_h.pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(gm_h.pill, GM_GREEN, 0);
    lv_obj_set_style_bg_opa(gm_h.pill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(gm_h.pill, 14, 0);
    lv_obj_set_style_pad_right(gm_h.pill, 14, 0);
    lv_obj_set_style_pad_top(gm_h.pill, 6, 0);
    lv_obj_set_style_pad_bottom(gm_h.pill, 6, 0);
    // CAR-278 review #7: pill is anchored 20px below the bar so a future
    // "both visible" bug reads as a vertical pair instead of an invisible
    // stack (bar sits at +140; pill at +160). The mode switch in
    // gm_status_apply_mode() currently hides one when the other is shown.
    lv_obj_align(gm_h.pill, LV_ALIGN_CENTER, 0, 160);
    lv_obj_add_flag(gm_h.pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(gm_h.pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(gm_h.pill, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *pill_label = lv_label_create(gm_h.pill);
    lv_label_set_text(pill_label, "READY");
    lv_obj_set_style_text_font(pill_label, &spacemono_14, 0);
    lv_obj_set_style_text_color(pill_label, GM_BG, 0);
    lv_obj_set_style_text_letter_space(pill_label, GM_TRACK_KICKER, 0);
    lv_obj_center(pill_label);

    // ── Bottom mode chip bar ──
    gm_chip_bar(ui_StatusScreen, 1, GM_RED); // brew chip lit by default
    // Wire tap navigation. chip0=Menu, chip1=Brew, chip2=Steam, chip3=Water.
    if (gm_h.chips[0]) lv_obj_add_event_cb(gm_h.chips[0], status_chip0_cb, LV_EVENT_ALL, NULL);
    if (gm_h.chips[1]) lv_obj_add_event_cb(gm_h.chips[1], status_chip1_cb, LV_EVENT_ALL, NULL);
    if (gm_h.chips[2]) lv_obj_add_event_cb(gm_h.chips[2], status_chip2_cb, LV_EVENT_ALL, NULL);
    if (gm_h.chips[3]) lv_obj_add_event_cb(gm_h.chips[3], status_chip3_cb, LV_EVENT_ALL, NULL);

    // Initial state: brew layout populated.
    gm_status_apply_mode(1, 0, 0);

    lv_obj_add_event_cb(ui_StatusScreen, ui_event_StatusScreen, LV_EVENT_ALL, NULL);
    // CAR-292: tap-to-toggle (water dispense) needs LV_EVENT_CLICKED to fire on
    // the screen body. Screens are not clickable by default; enabling here is
    // safe — no inner widget consumes clicks except the chip bar children
    // (decorative; bubbling triggers the same handler with the same WATER-only
    // gate inside onStatusScreenTap).
    lv_obj_add_flag(ui_StatusScreen, LV_OBJ_FLAG_CLICKABLE);
}

void ui_StatusScreen_screen_destroy(void) {
    if (ui_StatusScreen)
        lv_obj_del(ui_StatusScreen);

    // NULL screen variables. Live handles are owned by the screen we just
    // deleted, so reset them to avoid dangling pointers if something
    // updates between destroy and the next init.
    ui_StatusScreen = NULL;
    gm_h.arc = NULL;
    gm_h.kicker = NULL;
    gm_h.hero = NULL;
    gm_h.hero_unit = NULL;
    gm_h.m_weight = gm_h.m_temp = gm_h.m_press = gm_h.m_flow = NULL;
    gm_h.w_target = gm_h.w_temp = gm_h.w_flow = NULL;
    gm_h.pill = NULL;
    gm_h.bar = NULL;
    // Status bar handle (owned by this screen while active).
    //
    // History: a plain unguarded `gm_h.status_time = NULL;` was added here
    // in CAR-278 round-4 (commit 12531166) to satisfy the gm_ui.h ownership
    // contract that destroy hooks must NULL handles they own. CAR-297
    // upgrades it to the guarded-clobber pattern below.
    //
    // RACE: DefaultUI::handleScreenChange() builds the next screen
    // (_ui_screen_change → target_init → gm_status_bar() → re-points
    // gm_h.status_time at the *next* screen's clock) BEFORE lv_obj_del(current)
    // fires this destroy hook. Unconditionally NULLing gm_h.status_time here
    // would clobber the new screen's freshly-stored handle, leaving e.g.
    // DefaultUI::updateStatusScreen() (DefaultUI.cpp:1543) with a null pointer
    // — the destination clock would stick at "--:--" forever after the
    // transition. So only NULL the global if it still refers to OUR clock
    // (i.e. nothing else has taken ownership yet — typical for terminal
    // teardown paths). ss_status_time always tracks the label this screen
    // owns; when the global has moved on, leave it alone. CAR-297 (mirrors
    // the CAR-294 GrindScreen guarded-clobber fix).
    if (gm_h.status_time == ss_status_time) {
        gm_h.status_time = NULL;
    }
    ss_status_time = NULL;
    for (int i = 0; i < 4; i++) gm_h.chips[i] = NULL;
}
