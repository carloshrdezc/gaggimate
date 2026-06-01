// CAR-294: ui_GrindScreen rebuilt as the Nothing-theme grinder control surface.
// Replaces the SquareLine-generated layout with gm_make_screen() + gm_ui
// builders + gm_theme palette tokens, while preserving every ui_GrindScreen_*
// global symbol that DefaultUI.cpp and ui_events.cpp depend on.
//
// The dials cluster is intentionally kept (ui_dials_create) so the live ring
// updates in DefaultUI (adjustDials / adjustHeatingIndicator / applyProcessRing
// against uic_GrindScreen_dials_tempGauge) keep working without touching the
// reactive plumbing.
//
// Theme: dark by default (GM_BG black surface, GM_CONTENT near-white text).
// On UI_THEME_LIGHT, DefaultUI's styleScreenBase repaints the bg to
// palette.surfaceBase so we expose ui_GrindScreen_apply_palette() that recolors
// every tracked themable child (kicker / labels / icons / button surfaces /
// status-bar children).
//
// Status bar (gm_status_bar) is owned by the active screen; gm_h.status_time
// must be NULLed in screen_destroy. See StatusScreen / StandbyScreen / BrewScreen.

#include "../ui.h"
#include "../components/ui_comp_dials.h" // UI_COMP_DIALS_* indices for ui_comp_get_child
#include "../gm_ui.h" // GM_* palette, gm_h handles, shared builders

// ── SquareLine globals preserved (DefaultUI / ui_events surface) ──
lv_obj_t *uic_GrindScreen_dials_tempText = NULL;
lv_obj_t *uic_GrindScreen_dials_pressureText = NULL;
lv_obj_t *uic_GrindScreen_dials_pressureTarget = NULL;
lv_obj_t *uic_GrindScreen_dials_pressureGauge = NULL;
lv_obj_t *uic_GrindScreen_dials_tempTarget = NULL;
lv_obj_t *uic_GrindScreen_dials_tempGauge = NULL;
lv_obj_t *ui_GrindScreen = NULL;
lv_obj_t *ui_GrindScreen_dials = NULL;
lv_obj_t *ui_GrindScreen_ImgButton2 = NULL;
lv_obj_t *ui_GrindScreen_contentPanel7 = NULL;
lv_obj_t *ui_GrindScreen_mainLabel7 = NULL;
lv_obj_t *ui_GrindScreen_startButton = NULL;
lv_obj_t *ui_GrindScreen_targetContainer = NULL;
lv_obj_t *ui_GrindScreen_targetDuration = NULL;
lv_obj_t *ui_GrindScreen_upDurationButton = NULL;
lv_obj_t *ui_GrindScreen_downDurationButton = NULL;
lv_obj_t *ui_GrindScreen_targetSymbol = NULL;
lv_obj_t *ui_GrindScreen_modeSwitch = NULL;
lv_obj_t *ui_GrindScreen_volumetricButton = NULL;
lv_obj_t *ui_GrindScreen_weightLabel = NULL;

// ── Themable-children tracking (CAR-279 / CAR-293 pattern) ──
// styleScreenBase() in DefaultUI repaints the bg to palette.surfaceBase on
// UI_THEME_LIGHT; without explicit recolor, GM_CONTENT-near-white text/icons
// vanish against the near-white bg. ui_GrindScreen_apply_palette() walks these
// arrays from DefaultUI::applyScreenVisualLanguage().
static lv_obj_t *gs_text_labels[8] = {NULL}; // primary text (weight, targetDuration, +/- glyphs)
static int gs_text_count = 0;
static lv_obj_t *gs_kicker_label = NULL; // mainLabel7 (kicker tone — palette.grind)
static lv_obj_t *gs_recolor_icons[8] = {NULL}; // icon recolor handles (back btn, start, target/volumetric, etc.)
static int gs_recolor_icon_count = 0;
// Status-bar handle: captured from gm_status_bar() so apply_palette() can
// recolor its wifi/bt icons + the clock label when the user switches to the
// light theme on non-AMOLED panels. Without this, gm_status_bar()'s hard-coded
// GM_MUTED / GM_CONTENT (designed for the dark OLED base) leaves the status
// bar near-white-on-white when styleScreenBase() repaints the screen bg.
static lv_obj_t *gs_status_bar = NULL;
static lv_obj_t *gs_button_surfaces[6] = {NULL}; // round button bg surfaces (down stepper, modeSwitch chip)
static int gs_button_surface_count = 0;
static lv_obj_t *gs_accent_surfaces[4] = {NULL}; // accent-colored button surfaces (start, up stepper)
static int gs_accent_surface_count = 0;

static void gs_track_text(lv_obj_t *o) {
    if (o != NULL && gs_text_count < (int)(sizeof(gs_text_labels) / sizeof(gs_text_labels[0]))) {
        gs_text_labels[gs_text_count++] = o;
    }
}
static void gs_track_icon(lv_obj_t *o) {
    if (o != NULL && gs_recolor_icon_count < (int)(sizeof(gs_recolor_icons) / sizeof(gs_recolor_icons[0]))) {
        gs_recolor_icons[gs_recolor_icon_count++] = o;
    }
}
static void gs_track_button_surface(lv_obj_t *o) {
    if (o != NULL && gs_button_surface_count < (int)(sizeof(gs_button_surfaces) / sizeof(gs_button_surfaces[0]))) {
        gs_button_surfaces[gs_button_surface_count++] = o;
    }
}
static void gs_track_accent_surface(lv_obj_t *o) {
    if (o != NULL && gs_accent_surface_count < (int)(sizeof(gs_accent_surfaces) / sizeof(gs_accent_surfaces[0]))) {
        gs_accent_surfaces[gs_accent_surface_count++] = o;
    }
}

// ── Event functions (signatures unchanged) ──
void ui_event_GrindScreen(lv_event_t *e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP) {
        lv_indev_wait_release(lv_indev_get_act());
        onMenuClick(e);
    }
    if (event_code == LV_EVENT_SCREEN_LOADED) {
        onGrindScreenLoad(e);
    }
}

void ui_event_GrindScreen_ImgButton2(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onMenuClick(e);
}

void ui_event_GrindScreen_startButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onGrindToggle(e);
}

void ui_event_GrindScreen_upDurationButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onGrindTimeRaise(e);
}

void ui_event_GrindScreen_downDurationButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onGrindTimeLower(e);
}

void ui_event_GrindScreen_modeSwitch(lv_event_t *e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED) onVolumetricClick(e);
    if (event_code == LV_EVENT_LONG_PRESSED) onVolumetricHold(e);
}

// ── Build helpers ──

// Stepper glyph button (round, hosting a +/- glyph). Tracks the surface and
// the glyph label for the theme palette pass. Mirrors bs_glyph_btn() in
// ui_BrewScreen.c so the two screens share a visual language.
static lv_obj_t *gs_glyph_btn(lv_obj_t *parent, const char *glyph, int diameter,
                              lv_color_t bg_col, lv_color_t text_col, bool accent_surface) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, diameter, diameter);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, bg_col, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, glyph);
    lv_obj_set_style_text_font(lab, &ndot_28, 0);
    lv_obj_set_style_text_color(lab, text_col, 0);
    lv_obj_center(lab);
    gs_track_text(lab);
    if (accent_surface)
        gs_track_accent_surface(btn);
    else
        gs_track_button_surface(btn);
    return btn;
}

// ── Build function ──
void ui_GrindScreen_screen_init(void) {
    gs_text_count = 0;
    gs_recolor_icon_count = 0;
    gs_button_surface_count = 0;
    gs_accent_surface_count = 0;
    gs_kicker_label = NULL;

    ui_GrindScreen = gm_make_screen();
    lv_obj_clear_flag(ui_GrindScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ui_GrindScreen, scr_unloaded_delete_cb, LV_EVENT_SCREEN_UNLOADED,
                        ui_GrindScreen_screen_destroy);

    // Top status bar (wifi · bt · clock + live dot). Owner; destroy hook NULLs gm_h.status_time.
    // Cache the bar handle so apply_palette() can recolor its children on
    // light-theme switches; gm_status_bar() hard-codes GM_MUTED / GM_CONTENT.
    gs_status_bar = gm_status_bar(ui_GrindScreen, true);

    // Dials cluster (preserved). DefaultUI's adjustDials / adjustHeatingIndicator and
    // applyProcessRing(uic_GrindScreen_dials_tempGauge,...) drive these without touching
    // the screen-level layout. Centered behind the panel.
    ui_GrindScreen_dials = ui_dials_create(ui_GrindScreen);
    lv_obj_set_x(ui_GrindScreen_dials, 0);
    lv_obj_set_y(ui_GrindScreen_dials, 0);

    // Bind the dials' internal child handles. DefaultUI's effects dereference
    // these (lv_label_set_text_fmt on tempText/pressureText, lv_arc_set_value
    // on the gauges, applyProcessRing(uic_GrindScreen_dials_tempGauge,...) etc.).
    // Without this, those globals stay NULL and the grind ring/temp updates
    // crash as soon as the GrindScreen effects run.
    uic_GrindScreen_dials_tempGauge = ui_comp_get_child(ui_GrindScreen_dials, UI_COMP_DIALS_TEMPGAUGE);
    uic_GrindScreen_dials_tempTarget = ui_comp_get_child(ui_GrindScreen_dials, UI_COMP_DIALS_TEMPTARGET);
    uic_GrindScreen_dials_pressureGauge = ui_comp_get_child(ui_GrindScreen_dials, UI_COMP_DIALS_PRESSUREGAUGE);
    uic_GrindScreen_dials_pressureTarget = ui_comp_get_child(ui_GrindScreen_dials, UI_COMP_DIALS_PRESSURETARGET);
    uic_GrindScreen_dials_pressureText = ui_comp_get_child(ui_GrindScreen_dials, UI_COMP_DIALS_PRESSURETEXT);
    uic_GrindScreen_dials_tempText = ui_comp_get_child(ui_GrindScreen_dials, UI_COMP_DIALS_TEMPTEXT);

    // Top-left back/menu button (round, gm_ic_back). Round-display branch in DefaultUI
    // re-aligns it via alignTopBackButton(); we just give it a sane default position.
    ui_GrindScreen_ImgButton2 = lv_imgbtn_create(ui_GrindScreen);
    lv_imgbtn_set_src(ui_GrindScreen_ImgButton2, LV_IMGBTN_STATE_RELEASED, NULL, &gm_ic_back, NULL);
    lv_obj_set_size(ui_GrindScreen_ImgButton2, 44, 44);
    lv_obj_align(ui_GrindScreen_ImgButton2, LV_ALIGN_TOP_LEFT, 28, 28);
    lv_obj_set_style_img_recolor(ui_GrindScreen_ImgButton2, GM_CONTENT, 0);
    lv_obj_set_style_img_recolor_opa(ui_GrindScreen_ImgButton2, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(ui_GrindScreen_ImgButton2, 20);
    lv_obj_add_event_cb(ui_GrindScreen_ImgButton2, ui_event_GrindScreen_ImgButton2, LV_EVENT_ALL, NULL);
    gs_track_icon(ui_GrindScreen_ImgButton2);

    // contentPanel7: invisible round container hosting the layout. Sized 372x372 by
    // DefaultUI on round displays; default-sized 360x360 here so flat-display path renders.
    ui_GrindScreen_contentPanel7 = lv_obj_create(ui_GrindScreen);
    lv_obj_remove_style_all(ui_GrindScreen_contentPanel7);
    lv_obj_set_size(ui_GrindScreen_contentPanel7, 360, 360);
    lv_obj_align(ui_GrindScreen_contentPanel7, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(ui_GrindScreen_contentPanel7, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_GrindScreen_contentPanel7, LV_OBJ_FLAG_CLICKABLE);

    // Kicker label ("GRIND" / "GRINDING") — DefaultUI sets text + tone from palette.grind.
    ui_GrindScreen_mainLabel7 = lv_label_create(ui_GrindScreen_contentPanel7);
    lv_label_set_text(ui_GrindScreen_mainLabel7, "GRIND");
    lv_obj_set_style_text_font(ui_GrindScreen_mainLabel7, &ndot_24, 0);
    lv_obj_set_style_text_color(ui_GrindScreen_mainLabel7, GM_BLUE, 0); // palette.grind default
    lv_obj_set_style_text_letter_space(ui_GrindScreen_mainLabel7, GM_TRACK_KICKER, 0);
    lv_obj_align(ui_GrindScreen_mainLabel7, LV_ALIGN_TOP_MID, 0, 34);
    gs_kicker_label = ui_GrindScreen_mainLabel7;

    // ── modeSwitch: pill chip with volumetric/scale icon + weight label ──
    // DefaultUI toggles its visibility (volumetricAvailable) and recolors the
    // surface/icon based on volumetricMode (active vs inactive). Default
    // styling here matches the inactive state (GM_SURFACE bg, GM_CONTENT text).
    ui_GrindScreen_modeSwitch = lv_obj_create(ui_GrindScreen_contentPanel7);
    lv_obj_remove_style_all(ui_GrindScreen_modeSwitch);
    lv_obj_set_size(ui_GrindScreen_modeSwitch, 180, 50);
    lv_obj_set_style_radius(ui_GrindScreen_modeSwitch, 22, 0);
    lv_obj_set_style_bg_color(ui_GrindScreen_modeSwitch, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui_GrindScreen_modeSwitch, LV_OPA_70, 0);
    lv_obj_align(ui_GrindScreen_modeSwitch, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_flex_flow(ui_GrindScreen_modeSwitch, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_GrindScreen_modeSwitch, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui_GrindScreen_modeSwitch, 8, 0);
    lv_obj_set_style_pad_left(ui_GrindScreen_modeSwitch, 14, 0);
    lv_obj_set_style_pad_right(ui_GrindScreen_modeSwitch, 14, 0);
    lv_obj_clear_flag(ui_GrindScreen_modeSwitch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui_GrindScreen_modeSwitch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui_GrindScreen_modeSwitch, ui_event_GrindScreen_modeSwitch,
                        LV_EVENT_ALL, NULL);
    // NOTE: modeSwitch is intentionally NOT tracked as a button surface.
    // DefaultUI's `volumetricMode` effect owns its background color (active state
    // uses GM_CONTENT/NiceWhite, inactive uses GM_SURFACE/Dark) and the effect
    // only reruns when `volumetricMode` changes. If we tracked it here,
    // ui_GrindScreen_apply_palette() — invoked every render via
    // applyScreenVisualLanguage() in DefaultUI::loop — would clobber the active
    // state back to GM_SURFACE on the next idle tick until the user toggled the
    // chip again. See PR #135 Codex review (P2). BrewScreen's modeSwitch is also
    // untracked, but for a different reason (its chip is stateless there).

    ui_GrindScreen_volumetricButton = lv_img_create(ui_GrindScreen_modeSwitch);
    lv_img_set_src(ui_GrindScreen_volumetricButton, &gm_ic_drop);
    lv_obj_set_style_img_recolor(ui_GrindScreen_volumetricButton, GM_CONTENT, 0);
    lv_obj_set_style_img_recolor_opa(ui_GrindScreen_volumetricButton, LV_OPA_COVER, 0);
    lv_obj_add_flag(ui_GrindScreen_volumetricButton, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_GrindScreen_volumetricButton, LV_OBJ_FLAG_SCROLLABLE);
    // NOTE: volumetricButton recolor is owned by DefaultUI's `volumetricMode`
    // effect (Dark vs NiceWhite contrast on the chip). Same rationale as
    // modeSwitch above — don't track for the generic palette pass.

    ui_GrindScreen_weightLabel = lv_label_create(ui_GrindScreen_modeSwitch);
    lv_label_set_text(ui_GrindScreen_weightLabel, "-");
    lv_obj_set_style_text_font(ui_GrindScreen_weightLabel, &ndot_24, 0);
    lv_obj_set_style_text_color(ui_GrindScreen_weightLabel, GM_CONTENT, 0);
    lv_obj_set_style_text_align(ui_GrindScreen_weightLabel, LV_TEXT_ALIGN_CENTER, 0);
    // NOTE: weightLabel text color is owned by DefaultUI's `volumetricMode`
    // effect (Dark on the active chip's NiceWhite bg vs NiceWhite on the
    // inactive chip's Dark bg). Same rationale as modeSwitch above — don't
    // track for the generic palette pass.

    // ── targetContainer: [icon] [duration/weight] [-] [+] ──
    // 304 wide gives room for the symbol on the left, the value in the middle,
    // and the down/up steppers stacked on the right with explicit x-offsets so
    // no two clickable buttons share the same align slot (CAR-293 review #134).
    // Layout:
    //   targetSymbol  at LV_ALIGN_LEFT_MID, +8  (40px, accent recolor — toggled
    //                                             between drop / clock by the
    //                                             volumetricMode effect)
    //   targetDuration centered (the value: "0:30" or "18.0g")
    //   downDurationButton at LV_ALIGN_RIGHT_MID, -56  (40px, GM_SURFACE)
    //   upDurationButton   at LV_ALIGN_RIGHT_MID, -6   (40px, GM_RED accent)
    ui_GrindScreen_targetContainer = lv_obj_create(ui_GrindScreen_contentPanel7);
    lv_obj_remove_style_all(ui_GrindScreen_targetContainer);
    lv_obj_set_size(ui_GrindScreen_targetContainer, 304, 54);
    lv_obj_set_style_radius(ui_GrindScreen_targetContainer, 22, 0);
    lv_obj_set_style_bg_color(ui_GrindScreen_targetContainer, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui_GrindScreen_targetContainer, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(ui_GrindScreen_targetContainer, 6, 0);
    lv_obj_align(ui_GrindScreen_targetContainer, LV_ALIGN_CENTER, 0, 28);
    lv_obj_clear_flag(ui_GrindScreen_targetContainer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui_GrindScreen_targetContainer, LV_OBJ_FLAG_SCROLLABLE);

    // targetSymbol: drop / clock indicator. The runtime effect in DefaultUI
    // (effect_mgr at line ~972) swaps this between the legacy ui_img_1424216268
    // (drop) and ui_img_360122106 (clock) on volumetricMode change, so the
    // initial src here is overwritten by the first effect tick. We start with
    // gm_ic_drop for visual continuity if the effect is delayed.
    ui_GrindScreen_targetSymbol = lv_img_create(ui_GrindScreen_targetContainer);
    lv_img_set_src(ui_GrindScreen_targetSymbol, &gm_ic_drop);
    lv_obj_set_style_img_recolor(ui_GrindScreen_targetSymbol, GM_BLUE, 0); // palette.accent default for grind
    lv_obj_set_style_img_recolor_opa(ui_GrindScreen_targetSymbol, LV_OPA_COVER, 0);
    lv_obj_align(ui_GrindScreen_targetSymbol, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_add_flag(ui_GrindScreen_targetSymbol, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_GrindScreen_targetSymbol, LV_OBJ_FLAG_SCROLLABLE);
    gs_track_icon(ui_GrindScreen_targetSymbol);

    ui_GrindScreen_targetDuration = lv_label_create(ui_GrindScreen_targetContainer);
    lv_label_set_text(ui_GrindScreen_targetDuration, "0:30");
    lv_obj_set_style_text_font(ui_GrindScreen_targetDuration, &ndot_24, 0);
    lv_obj_set_style_text_color(ui_GrindScreen_targetDuration, GM_CONTENT, 0);
    lv_obj_set_style_text_align(ui_GrindScreen_targetDuration, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui_GrindScreen_targetDuration, LV_ALIGN_CENTER, 8, 0);
    gs_track_text(ui_GrindScreen_targetDuration);

    // downDurationButton (-) — left of upDurationButton at the right edge.
    ui_GrindScreen_downDurationButton = gs_glyph_btn(ui_GrindScreen_targetContainer, "-", 40,
                                                     GM_SURFACE, GM_CONTENT, false);
    lv_obj_align(ui_GrindScreen_downDurationButton, LV_ALIGN_RIGHT_MID, -56, 0);
    lv_obj_set_ext_click_area(ui_GrindScreen_downDurationButton, 15);
    lv_obj_add_event_cb(ui_GrindScreen_downDurationButton, ui_event_GrindScreen_downDurationButton,
                        LV_EVENT_ALL, NULL);

    // upDurationButton (+) — far right, accent surface.
    ui_GrindScreen_upDurationButton = gs_glyph_btn(ui_GrindScreen_targetContainer, "+", 40,
                                                   GM_BLUE, GM_CONTENT, true);
    lv_obj_align(ui_GrindScreen_upDurationButton, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_ext_click_area(ui_GrindScreen_upDurationButton, 15);
    lv_obj_add_event_cb(ui_GrindScreen_upDurationButton, ui_event_GrindScreen_upDurationButton,
                        LV_EVENT_ALL, NULL);

    // ── startButton: large round below targetContainer ──
    // Uses gm_ic_cup as the default glyph. DefaultUI's grindActive effect swaps
    // the imgbtn src to ui_img_1456692430 / ui_img_445946954 (legacy assets) to
    // toggle between idle and active iconography.
    ui_GrindScreen_startButton = lv_imgbtn_create(ui_GrindScreen_contentPanel7);
    lv_imgbtn_set_src(ui_GrindScreen_startButton, LV_IMGBTN_STATE_RELEASED, NULL, &gm_ic_cup, NULL);
    lv_obj_set_size(ui_GrindScreen_startButton, 64, 64);
    lv_obj_set_style_radius(ui_GrindScreen_startButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_GrindScreen_startButton, GM_BLUE, 0); // palette.accent default for grind
    lv_obj_set_style_bg_opa(ui_GrindScreen_startButton, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(ui_GrindScreen_startButton, GM_BG, 0);
    lv_obj_set_style_img_recolor_opa(ui_GrindScreen_startButton, LV_OPA_COVER, 0);
    lv_obj_align(ui_GrindScreen_startButton, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_ext_click_area(ui_GrindScreen_startButton, 25);
    lv_obj_add_event_cb(ui_GrindScreen_startButton, ui_event_GrindScreen_startButton,
                        LV_EVENT_ALL, NULL);
    gs_track_accent_surface(ui_GrindScreen_startButton);
    gs_track_icon(ui_GrindScreen_startButton);

    // Wire the screen-level event handler (gesture + screen_loaded).
    lv_obj_add_event_cb(ui_GrindScreen, ui_event_GrindScreen, LV_EVENT_ALL, NULL);
}

void ui_GrindScreen_screen_destroy(void) {
    if (ui_GrindScreen)
        lv_obj_del(ui_GrindScreen);

    // NULL all SquareLine globals so DefaultUI's lv_obj_is_valid() guards trip
    // correctly between destroy and the next init.
    ui_GrindScreen = NULL;
    ui_GrindScreen_dials = NULL;
    ui_GrindScreen_ImgButton2 = NULL;
    ui_GrindScreen_contentPanel7 = NULL;
    ui_GrindScreen_mainLabel7 = NULL;
    ui_GrindScreen_startButton = NULL;
    ui_GrindScreen_targetContainer = NULL;
    ui_GrindScreen_targetDuration = NULL;
    ui_GrindScreen_upDurationButton = NULL;
    ui_GrindScreen_downDurationButton = NULL;
    ui_GrindScreen_targetSymbol = NULL;
    ui_GrindScreen_modeSwitch = NULL;
    ui_GrindScreen_volumetricButton = NULL;
    ui_GrindScreen_weightLabel = NULL;

    // Dials children (custom UI components)
    uic_GrindScreen_dials_tempGauge = NULL;
    uic_GrindScreen_dials_tempTarget = NULL;
    uic_GrindScreen_dials_pressureGauge = NULL;
    uic_GrindScreen_dials_pressureTarget = NULL;
    uic_GrindScreen_dials_pressureText = NULL;
    uic_GrindScreen_dials_tempText = NULL;

    // Status bar handle (owned by this screen while active)
    gm_h.status_time = NULL;
    gs_status_bar = NULL;

    // Theme-tracking arrays
    gs_text_count = 0;
    gs_recolor_icon_count = 0;
    gs_button_surface_count = 0;
    gs_accent_surface_count = 0;
    gs_kicker_label = NULL;
    for (int i = 0; i < (int)(sizeof(gs_text_labels) / sizeof(gs_text_labels[0])); i++)
        gs_text_labels[i] = NULL;
    for (int i = 0; i < (int)(sizeof(gs_recolor_icons) / sizeof(gs_recolor_icons[0])); i++)
        gs_recolor_icons[i] = NULL;
    for (int i = 0; i < (int)(sizeof(gs_button_surfaces) / sizeof(gs_button_surfaces[0])); i++)
        gs_button_surfaces[i] = NULL;
    for (int i = 0; i < (int)(sizeof(gs_accent_surfaces) / sizeof(gs_accent_surfaces[0])); i++)
        gs_accent_surfaces[i] = NULL;
}

// CAR-294 / CAR-279 / CAR-293 pattern: recolor children with active palette so
// the screen stays legible on UI_THEME_LIGHT (where styleScreenBase repaints
// the bg from GM_BG black to a near-white surface).
//   text         - primary text (labels, stepper glyphs, weight, targetDuration)
//   muted        - secondary captions (none on GrindScreen today; reserved)
//   buttonSurface- minus stepper bg + modeSwitch chip bg
//   accent       - plus stepper / start button bg + targetSymbol icon tint
//   kickerTone   - mainLabel7 tone (matches palette.grind — DefaultUI overrides
//                  this immediately after, but pass it through here so a theme
//                  switch alone stays consistent if the ring update doesn't fire).
void ui_GrindScreen_apply_palette(lv_color_t text, lv_color_t muted,
                                  lv_color_t buttonSurface, lv_color_t accent,
                                  lv_color_t kickerTone) {
    if (gs_kicker_label != NULL && lv_obj_is_valid(gs_kicker_label)) {
        lv_obj_set_style_text_color(gs_kicker_label, kickerTone, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    for (int i = 0; i < gs_text_count; i++) {
        lv_obj_t *o = gs_text_labels[i];
        if (o != NULL && lv_obj_is_valid(o)) {
            lv_obj_set_style_text_color(o, text, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    for (int i = 0; i < gs_recolor_icon_count; i++) {
        lv_obj_t *o = gs_recolor_icons[i];
        if (o != NULL && lv_obj_is_valid(o)) {
            // The targetSymbol and the start-button glyph are accent-tinted, but
            // re-tinting them with `text` here is fine: DefaultUI's ring/start
            // effects re-apply the accent immediately after this pass. The back
            // button + volumetricButton + start button icon are intentionally
            // textPrimary on light themes for legibility against accent surfaces.
            lv_obj_set_style_img_recolor(o, text, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor_opa(o, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    for (int i = 0; i < gs_button_surface_count; i++) {
        lv_obj_t *o = gs_button_surfaces[i];
        if (o != NULL && lv_obj_is_valid(o)) {
            lv_obj_set_style_bg_color(o, buttonSurface, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    for (int i = 0; i < gs_accent_surface_count; i++) {
        lv_obj_t *o = gs_accent_surfaces[i];
        if (o != NULL && lv_obj_is_valid(o)) {
            lv_obj_set_style_bg_color(o, accent, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    // Re-tint the targetSymbol with the accent color (the generic icon-recolor
    // pass above sets it to `text`, which loses the accent identity). DefaultUI's
    // volumetricMode effect doesn't recolor this icon, so we own its tint.
    if (ui_GrindScreen_targetSymbol != NULL && lv_obj_is_valid(ui_GrindScreen_targetSymbol)) {
        lv_obj_set_style_img_recolor(ui_GrindScreen_targetSymbol, accent, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_img_recolor_opa(ui_GrindScreen_targetSymbol, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // Recolor the gm_status_bar() children. The builder hard-codes GM_MUTED
    // for the wifi/bt icons and GM_CONTENT for the clock label, which were
    // designed for the dark OLED base. On UI_THEME_LIGHT (non-AMOLED panels)
    // styleScreenBase() repaints the screen background to white and those
    // hard-coded near-white tones become invisible. Walk the bar's children
    // and retint: icons get the muted tone, clock gets primary text. The
    // live-dot (last child when present) is left alone — its green is a
    // semantic accent, not a theme tone.
    //
    // NOTE: We identify the clock by lv_label_class rather than by pointer
    // equality with gm_h.status_time. DefaultUI::handleScreenChange() runs
    // _ui_screen_change (which builds the new screen and calls
    // gm_status_bar() — re-pointing gm_h.status_time at the *new* clock)
    // BEFORE lv_obj_del(current) destroys the previous screen. The previous
    // screen's destroy hook then NULLs gm_h.status_time, clobbering the
    // freshly-stored handle. Pointer comparison would fail and the clock
    // would stay at hard-coded near-white on light backgrounds. The status
    // bar only has one label child (wifi/bt are images, live-dot is a bare
    // lv_obj), so class-based detection is unambiguous.
    if (gs_status_bar != NULL && lv_obj_is_valid(gs_status_bar)) {
        const uint32_t child_count = lv_obj_get_child_cnt(gs_status_bar);
        for (uint32_t i = 0; i < child_count; i++) {
            lv_obj_t *child = lv_obj_get_child(gs_status_bar, i);
            if (child == NULL || !lv_obj_is_valid(child)) continue;
            if (lv_obj_check_type(child, &lv_label_class)) {
                // Clock label — only label child of the status bar.
                lv_obj_set_style_text_color(child, text, LV_PART_MAIN | LV_STATE_DEFAULT);
            } else if (lv_obj_check_type(child, &lv_img_class)) {
                // Wifi / bt status icons.
                lv_obj_set_style_img_recolor(child, muted, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_img_recolor_opa(child, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
            }
            // Live dot is an lv_obj with a bg_color (not an image, not a
            // label) — leave its green intact.
        }
    }
}
