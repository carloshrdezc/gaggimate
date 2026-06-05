// CAR-293: ui_BrewScreen rebuilt as the Nothing-theme brew control surface.
// Replaces the SquareLine-generated layout with gm_make_screen() + gm_ui
// builders + gm_theme palette tokens, while preserving every ui_BrewScreen_*
// global symbol that DefaultUI.cpp and ui_events.cpp depend on (110+ refs).
//
// The dials cluster is intentionally kept (ui_dials_create) so the live ring
// updates in DefaultUI (adjustDials / adjustHeatingIndicator / applyProcessRing
// against uic_BrewScreen_dials_tempGauge) keep working without touching the
// reactive plumbing.
//
// Theme: dark by default (GM_BG black surface, GM_CONTENT near-white text).
// On UI_THEME_LIGHT, DefaultUI's styleScreenBase repaints the bg to
// palette.surfaceBase so we expose ui_BrewScreen_apply_palette() that recolors
// every tracked themable child (kicker / labels / icons / button surfaces).
//
// Status bar (gm_status_bar) is owned by the active screen; gm_h.status_time
// must be NULLed in screen_destroy. See StatusScreen / StandbyScreen.

#include "../ui.h"
#include "../components/ui_comp_dials.h" // UI_COMP_DIALS_* indices for ui_comp_get_child
#include "../gm_ui.h" // GM_* palette, gm_h handles, shared builders

// ── SquareLine globals preserved (DefaultUI / ui_events surface) ──
lv_obj_t *uic_BrewScreen_dials_tempText = NULL;
lv_obj_t *uic_BrewScreen_dials_pressureText = NULL;
lv_obj_t *uic_BrewScreen_dials_pressureTarget = NULL;
lv_obj_t *uic_BrewScreen_dials_pressureGauge = NULL;
lv_obj_t *uic_BrewScreen_dials_tempTarget = NULL;
lv_obj_t *uic_BrewScreen_dials_tempGauge = NULL;
lv_obj_t *ui_BrewScreen = NULL;
lv_obj_t *ui_BrewScreen_dials = NULL;
lv_obj_t *ui_BrewScreen_ImgButton5 = NULL;
lv_obj_t *ui_BrewScreen_contentPanel4 = NULL;
lv_obj_t *ui_BrewScreen_mainLabel3 = NULL;
lv_obj_t *ui_BrewScreen_startButton = NULL;
lv_obj_t *ui_BrewScreen_controlContainer = NULL;
lv_obj_t *ui_BrewScreen_modeSwitch = NULL;
lv_obj_t *ui_BrewScreen_volumetricButton = NULL;
lv_obj_t *ui_BrewScreen_weightLabel = NULL;
lv_obj_t *ui_BrewScreen_profileInfo = NULL;
lv_obj_t *ui_BrewScreen_Label1 = NULL;
lv_obj_t *ui_BrewScreen_Container3 = NULL;
lv_obj_t *ui_BrewScreen_profileSelectBtn = NULL;
lv_obj_t *ui_BrewScreen_profileName = NULL;
lv_obj_t *ui_BrewScreen_settingsButton = NULL;
lv_obj_t *ui_BrewScreen_adjustments = NULL;
lv_obj_t *ui_BrewScreen_tempContainer = NULL;
lv_obj_t *ui_BrewScreen_targetTemp = NULL;
lv_obj_t *ui_BrewScreen_downTempButton = NULL;
lv_obj_t *ui_BrewScreen_upTempButton = NULL;
lv_obj_t *ui_BrewScreen_Image5 = NULL;
lv_obj_t *ui_BrewScreen_targetContainer = NULL;
lv_obj_t *ui_BrewScreen_targetDuration = NULL;
lv_obj_t *ui_BrewScreen_upDurationButton = NULL;
lv_obj_t *ui_BrewScreen_downDurationButton = NULL;
lv_obj_t *ui_BrewScreen_Image4 = NULL;
lv_obj_t *ui_BrewScreen_byTimeButton = NULL;
lv_obj_t *ui_BrewScreen_saveButton = NULL;
lv_obj_t *ui_BrewScreen_acceptButton = NULL;
lv_obj_t *ui_BrewScreen_saveAsNewButton = NULL;

// ── Themable-children tracking (CAR-279 pattern) ──
// styleScreenBase() in DefaultUI repaints the bg to palette.surfaceBase on
// UI_THEME_LIGHT; without explicit recolor, GM_CONTENT-near-white text/icons
// vanish against the near-white bg. ui_BrewScreen_apply_palette() walks these
// arrays from DefaultUI::applyScreenVisualLanguage().
static lv_obj_t *bs_text_labels[12] = {NULL}; // primary text (Label1, weight, profileName, targetTemp, targetDuration, +/- glyphs)
static int bs_text_count = 0;
static lv_obj_t *bs_kicker_label = NULL; // mainLabel3 (kicker tone)
static lv_obj_t *bs_recolor_icons[14] = {NULL}; // icon recolor handles (Image4/Image5/back/profile/settings/etc.)
static int bs_recolor_icon_count = 0;
// Status-bar handle: captured from gm_status_bar() so apply_palette() can
// recolor its wifi/bt icons + the clock label when the user switches to the
// light theme on non-AMOLED panels. Without this, gm_status_bar()'s hard-coded
// GM_MUTED / GM_CONTENT (designed for the dark OLED base) leaves the status
// bar near-white-on-white when styleScreenBase() repaints the screen bg.
static lv_obj_t *bs_status_bar = NULL;
// Local snapshot of the clock label this screen owns. Captured from
// gm_h.status_time immediately after gm_status_bar() builds it. Used by the
// destroy hook to decide whether the global still belongs to *this* screen
// (see screen_destroy below for the full rationale). CAR-297 (mirrors the
// CAR-294 GrindScreen guarded-clobber fix).
static lv_obj_t *bs_status_time = NULL;
static lv_obj_t *bs_button_surfaces[8] = {NULL}; // round button bg surfaces (down/up steppers, save, etc.)
static int bs_button_surface_count = 0;
static lv_obj_t *bs_accent_surfaces[8] = {NULL}; // accent-colored button surfaces (start, up, profileSelect, accept)
static int bs_accent_surface_count = 0;

// CAR-301 chat2 redesign — file-static handles for the new BrewIdle layout.
//   uic_BrewScreen_hero_value : centered hero numeral ("--" → live temp; ndot_150)
//   uic_BrewScreen_hero_unit  : "°" suffix (ndot_60, GM_CONTENT)
//   uic_BrewScreen_status_pill: pill bg wrapping ui_BrewScreen_mainLabel3 (rounded chip with
//                  GM_HEAT/GM_RED border tone — DefaultUI overwrites mainLabel3
//                  text/colour from ringVisual every render)
//   uic_BrewScreen_ratio_sub  : small muted line beneath status pill ("18 → 36 · 1:2 · 9 BAR")
//   s_start_label: "START SHOT" label inside the resized pill startButton
// These are NOT part of the SquareLine global surface — internal-only, NULLed
// in screen_destroy alongside the bs_* tracking arrays.
// CAR-301 review C2/C3: hero numeral and ratio sub-line are now driven by
// DefaultUI effects (live currentTemp + selectedProfile fields), so these
// three are exported as `uic_BrewScreen_*` custom variables matching the
// existing dials/tempText convention. status_pill and start_label remain
// file-static — DefaultUI never touches them directly.
lv_obj_t *uic_BrewScreen_hero_value = NULL;
lv_obj_t *uic_BrewScreen_hero_unit = NULL;
// CAR-315: exported (was file-static uic_BrewScreen_status_pill) so DefaultUI's
// brewScreenState effect can hide it in the Settings sub-state and re-anchor
// it as the top kicker in the landing sub-state.
lv_obj_t *uic_BrewScreen_status_pill = NULL;
lv_obj_t *uic_BrewScreen_ratio_sub = NULL;
lv_obj_t *uic_BrewScreen_save_label = NULL;    // CAR-325
lv_obj_t *uic_BrewScreen_save_as_label = NULL; // CAR-325
static lv_obj_t *s_start_label = NULL;

static void bs_track_text(lv_obj_t *o) {
    if (o != NULL && bs_text_count < (int)(sizeof(bs_text_labels) / sizeof(bs_text_labels[0]))) {
        bs_text_labels[bs_text_count++] = o;
    }
}
static void bs_track_icon(lv_obj_t *o) {
    if (o != NULL && bs_recolor_icon_count < (int)(sizeof(bs_recolor_icons) / sizeof(bs_recolor_icons[0]))) {
        bs_recolor_icons[bs_recolor_icon_count++] = o;
    }
}
static void bs_track_button_surface(lv_obj_t *o) {
    if (o != NULL && bs_button_surface_count < (int)(sizeof(bs_button_surfaces) / sizeof(bs_button_surfaces[0]))) {
        bs_button_surfaces[bs_button_surface_count++] = o;
    }
}
static void bs_track_accent_surface(lv_obj_t *o) {
    if (o != NULL && bs_accent_surface_count < (int)(sizeof(bs_accent_surfaces) / sizeof(bs_accent_surfaces[0]))) {
        bs_accent_surfaces[bs_accent_surface_count++] = o;
    }
}

// ── Event functions (signatures unchanged) ──
void ui_event_BrewScreen(lv_event_t *e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP) {
        lv_indev_wait_release(lv_indev_get_act());
        onMenuClick(e);
    }
    if (event_code == LV_EVENT_SCREEN_LOADED) {
        onBrewScreenLoad(e);
    }
}

void ui_event_BrewScreen_ImgButton5(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onMenuClick(e);
}

void ui_event_BrewScreen_startButton(lv_event_t *e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED) onBrewStart(e);
    if (event_code == LV_EVENT_LONG_PRESSED) onFlush(e);
}

void ui_event_BrewScreen_modeSwitch(lv_event_t *e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED) onVolumetricClick(e);
    if (event_code == LV_EVENT_LONG_PRESSED) onVolumetricHold(e);
}

void ui_event_BrewScreen_profileSelectBtn(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onProfileSelect(e);
}

void ui_event_BrewScreen_settingsButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onProfileSettings(e);
}

void ui_event_BrewScreen_downTempButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onBrewTempLower(e);
}

void ui_event_BrewScreen_upTempButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onBrewTempRaise(e);
}

void ui_event_BrewScreen_upDurationButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onBrewTimeRaise(e);
}

void ui_event_BrewScreen_downDurationButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onBrewTimeLower(e);
}

void ui_event_BrewScreen_byTimeButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onVolumetricDelete(e);
}

void ui_event_BrewScreen_saveButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onProfileSave(e);
}

void ui_event_BrewScreen_acceptButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onProfileAccept(e);
}

void ui_event_BrewScreen_saveAsNewButton(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) onProfileSaveAsNew(e);
}

// ── Build helpers ──

// Stepper glyph button (round, hosting a +/- glyph). Tracks the surface and
// the glyph label for the theme palette pass.
static lv_obj_t *bs_glyph_btn(lv_obj_t *parent, const char *glyph, int diameter,
                              lv_color_t bg_col, lv_color_t text_col, bool accent_surface) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, diameter, diameter);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, bg_col, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    // CAR-326: the default LVGL theme paints a red pressed/checked/focused state.
    // Pin every interactive state to the resting bg + opacity and zero the focus
    // outline so the steppers never flash red when touched.
    lv_obj_set_style_bg_color(btn, bg_col, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, bg_col, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(btn, bg_col, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(btn, 0, LV_STATE_PRESSED);

    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, glyph);
    // CAR-324: render the +/- stepper glyphs in grotesk (clean sans) rather than
    // ndot_28 (dot-matrix), whose "+" comes out sparse/asymmetric and reads as
    // broken next to the digit readouts.
    lv_obj_set_style_text_font(lab, &grotesk_16, 0);
    lv_obj_set_style_text_color(lab, text_col, 0);
    lv_obj_center(lab);
    bs_track_text(lab);
    if (accent_surface)
        bs_track_accent_surface(btn);
    else
        bs_track_button_surface(btn);
    return btn;
}

// ── Build function ──
void ui_BrewScreen_screen_init(void) {
    bs_text_count = 0;
    bs_recolor_icon_count = 0;
    bs_button_surface_count = 0;
    bs_accent_surface_count = 0;
    bs_kicker_label = NULL;

    ui_BrewScreen = gm_make_screen();
    lv_obj_clear_flag(ui_BrewScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ui_BrewScreen, scr_unloaded_delete_cb, LV_EVENT_SCREEN_UNLOADED,
                        ui_BrewScreen_screen_destroy);

    // Top status bar (wifi · bt · clock + live dot). Owner; destroy hook NULLs gm_h.status_time.
    // Cache the bar handle so apply_palette() can recolor its children on
    // light-theme switches; gm_status_bar() hard-codes GM_MUTED / GM_CONTENT.
    bs_status_bar = gm_status_bar(ui_BrewScreen, true);
    // Snapshot the clock label gm_status_bar() just stored in the global, so
    // the destroy hook can tell whether gm_h.status_time still points at our
    // own clock (vs. having been overwritten by the next screen's init).
    // CAR-297 (mirrors CAR-294 GrindScreen guarded-clobber).
    bs_status_time = gm_h.status_time;

    // Dials cluster (preserved). DefaultUI's adjustDials / adjustHeatingIndicator and
    // applyProcessRing(uic_BrewScreen_dials_tempGauge,...) drive these without touching
    // the screen-level layout. Centered behind the panel.
    ui_BrewScreen_dials = ui_dials_create(ui_BrewScreen);
    lv_obj_set_x(ui_BrewScreen_dials, 0);
    lv_obj_set_y(ui_BrewScreen_dials, 0);

    // Bind the dials' internal child handles. DefaultUI's effects dereference
    // these (lv_label_set_text_fmt on tempText/pressureText, lv_arc_set_value
    // on the gauges, applyProcessRing(uic_BrewScreen_dials_tempGauge,...) etc.).
    // Without this, those globals stay NULL and the brew ring/temp/pressure
    // updates crash as soon as the BrewScreen effects run. Mirrors the
    // SquareLine-generated pattern that the original ui_BrewScreen_screen_init
    // performed right after ui_dials_create().
    uic_BrewScreen_dials_tempGauge = ui_comp_get_child(ui_BrewScreen_dials, UI_COMP_DIALS_TEMPGAUGE);
    uic_BrewScreen_dials_tempTarget = ui_comp_get_child(ui_BrewScreen_dials, UI_COMP_DIALS_TEMPTARGET);
    uic_BrewScreen_dials_pressureGauge = ui_comp_get_child(ui_BrewScreen_dials, UI_COMP_DIALS_PRESSUREGAUGE);
    uic_BrewScreen_dials_pressureTarget = ui_comp_get_child(ui_BrewScreen_dials, UI_COMP_DIALS_PRESSURETARGET);
    uic_BrewScreen_dials_pressureText = ui_comp_get_child(ui_BrewScreen_dials, UI_COMP_DIALS_PRESSURETEXT);
    uic_BrewScreen_dials_tempText = ui_comp_get_child(ui_BrewScreen_dials, UI_COMP_DIALS_TEMPTEXT);

    // Top-left back/menu button (round, gm_ic_back). Round-display branch in DefaultUI
    // re-aligns it via alignTopBackButton(); we just give it a sane default position.
    ui_BrewScreen_ImgButton5 = lv_imgbtn_create(ui_BrewScreen);
    lv_imgbtn_set_src(ui_BrewScreen_ImgButton5, LV_IMGBTN_STATE_RELEASED, NULL, &gm_ic_back, NULL);
    lv_obj_set_size(ui_BrewScreen_ImgButton5, 44, 44);
    lv_obj_align(ui_BrewScreen_ImgButton5, LV_ALIGN_TOP_LEFT, 28, 28);
    lv_obj_set_style_img_recolor(ui_BrewScreen_ImgButton5, GM_CONTENT, 0);
    lv_obj_set_style_img_recolor_opa(ui_BrewScreen_ImgButton5, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(ui_BrewScreen_ImgButton5, 20);
    lv_obj_add_event_cb(ui_BrewScreen_ImgButton5, ui_event_BrewScreen_ImgButton5, LV_EVENT_ALL, NULL);
    bs_track_icon(ui_BrewScreen_ImgButton5);

    // contentPanel4: invisible round container hosting the layout. Sized 372x372 by
    // DefaultUI on round displays; default-sized 360x360 here so flat-display path renders.
    ui_BrewScreen_contentPanel4 = lv_obj_create(ui_BrewScreen);
    lv_obj_remove_style_all(ui_BrewScreen_contentPanel4);
    lv_obj_set_size(ui_BrewScreen_contentPanel4, 360, 360);
    lv_obj_align(ui_BrewScreen_contentPanel4, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(ui_BrewScreen_contentPanel4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_BrewScreen_contentPanel4, LV_OBJ_FLAG_CLICKABLE);

    // ── CAR-301 chat2 redesign: hero numeral, status pill, ratio sub ──
    //
    // Layout (480×480, contentPanel4 360×360 centered):
    //   profileInfo (chip)  TOP_MID  y=80   — repositioned below
    //   hero (uic_BrewScreen_hero_value) CENTER   y=-10  — large ndot_150 numeral
    //     + uic_BrewScreen_hero_unit "°" sits OUT_RIGHT_BOTTOM of hero
    //   uic_BrewScreen_status_pill       CENTER   y=90   — wraps ui_BrewScreen_mainLabel3
    //   uic_BrewScreen_ratio_sub         CENTER   y=130  — muted ratio breakdown
    //   startButton (pill)  BOTTOM_MID y=-54 — full-width red "START SHOT"
    //
    // The kicker text ("BREW") was removed in chat2 — the status pill now
    // carries the brew-state phrase. ui_BrewScreen_mainLabel3 is still created
    // (DefaultUI.cpp:1284-1287 overwrites its text from ringVisual.title every
    // render) but lives INSIDE uic_BrewScreen_status_pill instead of free-floating at the
    // top of the panel.

    // Hero numeral — centered, ndot_150, GM_CONTENT.
    // CAR-301 review C2: wired to live currentTemp via DefaultUI effect
    // (DefaultUI.cpp brew-screen temp effect now also writes
    // uic_BrewScreen_hero_value alongside uic_BrewScreen_dials_tempText).
    // Initial "--" placeholder is overwritten on first temp tick.
    uic_BrewScreen_hero_value = lv_label_create(ui_BrewScreen_contentPanel4);
    lv_label_set_text(uic_BrewScreen_hero_value, "--");
    lv_obj_set_style_text_font(uic_BrewScreen_hero_value, &ndot_150, 0);
    lv_obj_set_style_text_color(uic_BrewScreen_hero_value, GM_CONTENT, 0);
    // Shift left by ~half the unit-suffix width so hero+unit read as a
    // visually-centered group (mirrors StatusScreen hero pattern).
    lv_obj_align(uic_BrewScreen_hero_value, LV_ALIGN_CENTER, -16, -10);
    bs_track_text(uic_BrewScreen_hero_value);

    uic_BrewScreen_hero_unit = lv_label_create(ui_BrewScreen_contentPanel4);
    lv_label_set_text(uic_BrewScreen_hero_unit, "\xC2\xB0"); // U+00B0 °
    lv_obj_set_style_text_font(uic_BrewScreen_hero_unit, &ndot_60, 0);
    lv_obj_set_style_text_color(uic_BrewScreen_hero_unit, GM_CONTENT, 0);
    lv_obj_align_to(uic_BrewScreen_hero_unit, uic_BrewScreen_hero_value, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -8);
    bs_track_text(uic_BrewScreen_hero_unit);

    // Status pill — rounded chip wrapping ui_BrewScreen_mainLabel3.
    // Tone: GM_HEAT (heating) or GM_RED (at-target / brew ready). DefaultUI
    // overrides mainLabel3 colour from ringVisual.tone every render, so the
    // visible state colour stays accurate; we set a sensible default here.
    uic_BrewScreen_status_pill = lv_obj_create(ui_BrewScreen_contentPanel4);
    lv_obj_remove_style_all(uic_BrewScreen_status_pill);
    lv_obj_set_size(uic_BrewScreen_status_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(uic_BrewScreen_status_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(uic_BrewScreen_status_pill, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(uic_BrewScreen_status_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(uic_BrewScreen_status_pill, GM_HEAT, 0);
    lv_obj_set_style_border_width(uic_BrewScreen_status_pill, 1, 0);
    lv_obj_set_style_border_opa(uic_BrewScreen_status_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(uic_BrewScreen_status_pill, 24, 0);
    lv_obj_set_style_pad_right(uic_BrewScreen_status_pill, 24, 0);
    lv_obj_set_style_pad_top(uic_BrewScreen_status_pill, 8, 0);
    lv_obj_set_style_pad_bottom(uic_BrewScreen_status_pill, 8, 0);
    lv_obj_align(uic_BrewScreen_status_pill, LV_ALIGN_CENTER, 0, 90);
    lv_obj_clear_flag(uic_BrewScreen_status_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(uic_BrewScreen_status_pill, LV_OBJ_FLAG_CLICKABLE);
    bs_track_button_surface(uic_BrewScreen_status_pill);

    // Kicker label ("BREW" / "READY" / etc.) — DefaultUI sets text + tone from ringVisual.
    // Now lives inside uic_BrewScreen_status_pill instead of free-floating; DefaultUI keeps
    // touching only the label, never the pill bg, so this is safe.
    ui_BrewScreen_mainLabel3 = lv_label_create(uic_BrewScreen_status_pill);
    lv_label_set_text(ui_BrewScreen_mainLabel3, "HEATING");
    lv_obj_set_style_text_font(ui_BrewScreen_mainLabel3, &ndot_24, 0);
    lv_obj_set_style_text_color(ui_BrewScreen_mainLabel3, GM_HEAT, 0);
    lv_obj_set_style_text_letter_space(ui_BrewScreen_mainLabel3, GM_TRACK_KICKER, 0);
    lv_obj_center(ui_BrewScreen_mainLabel3);
    bs_kicker_label = ui_BrewScreen_mainLabel3;

    // Ratio sub-line — small muted breakdown beneath the status pill.
    // CAR-301 review C3: wired from selectedProfile via DefaultUI effect
    // (target volume + duration + max brew-phase pressure). Static "--"
    // placeholder is overwritten on first profile tick.
    uic_BrewScreen_ratio_sub = lv_label_create(ui_BrewScreen_contentPanel4);
    lv_label_set_text(uic_BrewScreen_ratio_sub, "--");
    lv_obj_set_style_text_font(uic_BrewScreen_ratio_sub, &spacemono_14, 0);
    lv_obj_set_style_text_color(uic_BrewScreen_ratio_sub, GM_MUTED, 0);
    lv_obj_set_style_text_letter_space(uic_BrewScreen_ratio_sub, 1, 0);
    lv_obj_align(uic_BrewScreen_ratio_sub, LV_ALIGN_CENTER, 0, 130);
    bs_track_text(uic_BrewScreen_ratio_sub);

    // controlContainer: column flex hosting profileInfo + adjustments + start area.
    // DefaultUI resizes/aligns this on round displays; default rectangular sizing here.
    ui_BrewScreen_controlContainer = lv_obj_create(ui_BrewScreen_contentPanel4);
    lv_obj_remove_style_all(ui_BrewScreen_controlContainer);
    lv_obj_set_size(ui_BrewScreen_controlContainer, 320, 230);
    lv_obj_align(ui_BrewScreen_controlContainer, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(ui_BrewScreen_controlContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_BrewScreen_controlContainer, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ui_BrewScreen_controlContainer, 10, 0);
    lv_obj_clear_flag(ui_BrewScreen_controlContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_BrewScreen_controlContainer, LV_OBJ_FLAG_CLICKABLE);

    // ── profileInfo: pill-style chip at TOP_MID (CAR-301 chat2 redesign) ──
    //
    // Was a multi-row stack inside controlContainer; now a single horizontal
    // pill chip parented to contentPanel4 directly so it floats independently
    // of the Settings adjustments column. DefaultUI.cpp:1097 hides this when
    // brewScreenState == Brew.
    ui_BrewScreen_profileInfo = lv_obj_create(ui_BrewScreen_contentPanel4);
    lv_obj_remove_style_all(ui_BrewScreen_profileInfo);
    lv_obj_set_size(ui_BrewScreen_profileInfo, 292, 100);
    // CAR-301 review C1: contentPanel4 has no flex layout, so without an
    // explicit align this chip lands at the parent's top-left default when
    // DefaultUI un-hides it in Settings sub-state. Anchor at TOP_MID y=80 to
    // match the layout comment above and keep the chip centered horizontally
    // when it becomes visible. (Was implicit in the old controlContainer flex
    // column; lost during the chat2 reparent.)
    lv_obj_align(ui_BrewScreen_profileInfo, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_radius(ui_BrewScreen_profileInfo, 28, 0);
    lv_obj_set_style_bg_color(ui_BrewScreen_profileInfo, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui_BrewScreen_profileInfo, LV_OPA_50, 0);
    lv_obj_set_flex_flow(ui_BrewScreen_profileInfo, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_BrewScreen_profileInfo, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(ui_BrewScreen_profileInfo, LV_OBJ_FLAG_HIDDEN); // toggled by DefaultUI
    lv_obj_clear_flag(ui_BrewScreen_profileInfo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui_BrewScreen_profileInfo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(ui_BrewScreen_profileInfo, 6, 0);
    lv_obj_set_style_pad_top(ui_BrewScreen_profileInfo, 8, 0);
    lv_obj_set_style_pad_bottom(ui_BrewScreen_profileInfo, 8, 0);

    ui_BrewScreen_Container3 = lv_obj_create(ui_BrewScreen_profileInfo);
    lv_obj_remove_style_all(ui_BrewScreen_Container3);
    lv_obj_set_size(ui_BrewScreen_Container3, 252, 44);
    lv_obj_set_flex_flow(ui_BrewScreen_Container3, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_BrewScreen_Container3, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui_BrewScreen_Container3, 12, 0);
    lv_obj_clear_flag(ui_BrewScreen_Container3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui_BrewScreen_Container3, LV_OBJ_FLAG_SCROLLABLE);

    // profileSelectBtn — left round button (accent surface, dark icon)
    ui_BrewScreen_profileSelectBtn = lv_imgbtn_create(ui_BrewScreen_Container3);
    lv_imgbtn_set_src(ui_BrewScreen_profileSelectBtn, LV_IMGBTN_STATE_RELEASED, NULL, &gm_ic_cup, NULL);
    lv_obj_set_size(ui_BrewScreen_profileSelectBtn, 40, 40);
    lv_obj_set_style_radius(ui_BrewScreen_profileSelectBtn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_BrewScreen_profileSelectBtn, GM_RED, 0);
    lv_obj_set_style_bg_opa(ui_BrewScreen_profileSelectBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(ui_BrewScreen_profileSelectBtn, GM_BG, 0);
    lv_obj_set_style_img_recolor_opa(ui_BrewScreen_profileSelectBtn, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(ui_BrewScreen_profileSelectBtn, 25);
    lv_obj_add_event_cb(ui_BrewScreen_profileSelectBtn, ui_event_BrewScreen_profileSelectBtn,
                        LV_EVENT_ALL, NULL);
    bs_track_accent_surface(ui_BrewScreen_profileSelectBtn);
    bs_track_icon(ui_BrewScreen_profileSelectBtn);

    // profileName — center label (large, scrolls if too long)
    ui_BrewScreen_profileName = lv_label_create(ui_BrewScreen_Container3);
    lv_obj_set_flex_grow(ui_BrewScreen_profileName, 1);
    lv_label_set_long_mode(ui_BrewScreen_profileName, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(ui_BrewScreen_profileName, "Profile");
    lv_obj_set_style_text_font(ui_BrewScreen_profileName, &ndot_24, 0);
    lv_obj_set_style_text_color(ui_BrewScreen_profileName, GM_CONTENT, 0);
    lv_obj_set_style_text_align(ui_BrewScreen_profileName, LV_TEXT_ALIGN_CENTER, 0);
    bs_track_text(ui_BrewScreen_profileName);

    // settingsButton — right round button (muted surface, neutral icon)
    ui_BrewScreen_settingsButton = lv_imgbtn_create(ui_BrewScreen_Container3);
    lv_imgbtn_set_src(ui_BrewScreen_settingsButton, LV_IMGBTN_STATE_RELEASED, NULL, &gm_ic_back, NULL);
    lv_obj_set_size(ui_BrewScreen_settingsButton, 40, 40);
    lv_obj_set_style_radius(ui_BrewScreen_settingsButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_BrewScreen_settingsButton, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui_BrewScreen_settingsButton, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(ui_BrewScreen_settingsButton, GM_MUTED, 0);
    lv_obj_set_style_img_recolor_opa(ui_BrewScreen_settingsButton, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(ui_BrewScreen_settingsButton, ui_event_BrewScreen_settingsButton,
                        LV_EVENT_ALL, NULL);
    bs_track_button_surface(ui_BrewScreen_settingsButton);
    bs_track_icon(ui_BrewScreen_settingsButton);

    // Label1 — secondary caption ("Selected profile" / brew context fallback). DefaultUI
    // overwrites this and ensureBrewContextLabel() may parent a context label sibling.
    ui_BrewScreen_Label1 = lv_label_create(ui_BrewScreen_profileInfo);
    lv_label_set_text(ui_BrewScreen_Label1, "Selected profile");
    lv_obj_set_style_text_font(ui_BrewScreen_Label1, &grotesk_16, 0);
    lv_obj_set_style_text_color(ui_BrewScreen_Label1, GM_MUTED, 0);
    lv_obj_set_style_text_align(ui_BrewScreen_Label1, LV_TEXT_ALIGN_CENTER, 0);
    bs_track_text(ui_BrewScreen_Label1);

    // ── modeSwitch: pill chip with volumetric icon + weight label ──
    ui_BrewScreen_modeSwitch = lv_obj_create(ui_BrewScreen_controlContainer);
    lv_obj_remove_style_all(ui_BrewScreen_modeSwitch);
    lv_obj_set_size(ui_BrewScreen_modeSwitch, 180, 50);
    lv_obj_set_style_radius(ui_BrewScreen_modeSwitch, 22, 0);
    lv_obj_set_style_bg_color(ui_BrewScreen_modeSwitch, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui_BrewScreen_modeSwitch, LV_OPA_70, 0);
    lv_obj_set_flex_flow(ui_BrewScreen_modeSwitch, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_BrewScreen_modeSwitch, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui_BrewScreen_modeSwitch, 8, 0);
    lv_obj_set_style_pad_left(ui_BrewScreen_modeSwitch, 14, 0);
    lv_obj_set_style_pad_right(ui_BrewScreen_modeSwitch, 14, 0);
    lv_obj_clear_flag(ui_BrewScreen_modeSwitch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui_BrewScreen_modeSwitch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui_BrewScreen_modeSwitch, ui_event_BrewScreen_modeSwitch,
                        LV_EVENT_ALL, NULL);

    ui_BrewScreen_volumetricButton = lv_img_create(ui_BrewScreen_modeSwitch);
    lv_img_set_src(ui_BrewScreen_volumetricButton, &gm_ic_drop);
    lv_obj_set_style_img_recolor(ui_BrewScreen_volumetricButton, GM_CONTENT, 0);
    lv_obj_set_style_img_recolor_opa(ui_BrewScreen_volumetricButton, LV_OPA_COVER, 0);
    bs_track_icon(ui_BrewScreen_volumetricButton);

    ui_BrewScreen_weightLabel = lv_label_create(ui_BrewScreen_modeSwitch);
    lv_label_set_text(ui_BrewScreen_weightLabel, "0.0g");
    lv_obj_set_style_text_font(ui_BrewScreen_weightLabel, &ndot_24, 0);
    lv_obj_set_style_text_color(ui_BrewScreen_weightLabel, GM_CONTENT, 0);
    lv_obj_set_style_text_align(ui_BrewScreen_weightLabel, LV_TEXT_ALIGN_CENTER, 0);
    bs_track_text(ui_BrewScreen_weightLabel);

    // ── adjustments: column hosting tempContainer + targetContainer ──
    // DefaultUI flips visibility based on BrewScreenState (Settings vs default).
    ui_BrewScreen_adjustments = lv_obj_create(ui_BrewScreen_controlContainer);
    lv_obj_remove_style_all(ui_BrewScreen_adjustments);
    lv_obj_set_size(ui_BrewScreen_adjustments, 300, 122);
    lv_obj_set_flex_flow(ui_BrewScreen_adjustments, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_BrewScreen_adjustments, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ui_BrewScreen_adjustments, 10, 0);
    lv_obj_clear_flag(ui_BrewScreen_adjustments, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui_BrewScreen_adjustments, LV_OBJ_FLAG_SCROLLABLE);

    // tempContainer: [-] [thermo] [target temp] [+]
    ui_BrewScreen_tempContainer = lv_obj_create(ui_BrewScreen_adjustments);
    lv_obj_remove_style_all(ui_BrewScreen_tempContainer);
    // CAR-326: width matches targetContainer (304px) so the temp row and the
    // time row below align in the center-stacked adjustments column (254px made
    // the temp row look indented).
    lv_obj_set_size(ui_BrewScreen_tempContainer, 304, 54);
    lv_obj_set_style_radius(ui_BrewScreen_tempContainer, 22, 0);
    lv_obj_set_style_bg_color(ui_BrewScreen_tempContainer, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui_BrewScreen_tempContainer, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(ui_BrewScreen_tempContainer, 6, 0);
    lv_obj_clear_flag(ui_BrewScreen_tempContainer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui_BrewScreen_tempContainer, LV_OBJ_FLAG_SCROLLABLE);

    ui_BrewScreen_downTempButton = bs_glyph_btn(ui_BrewScreen_tempContainer, "-", 40,
                                                GM_SURFACE, GM_CONTENT, false);
    lv_obj_align(ui_BrewScreen_downTempButton, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_ext_click_area(ui_BrewScreen_downTempButton, 15);
    lv_obj_add_event_cb(ui_BrewScreen_downTempButton, ui_event_BrewScreen_downTempButton,
                        LV_EVENT_ALL, NULL);

    ui_BrewScreen_Image5 = lv_img_create(ui_BrewScreen_tempContainer);
    lv_img_set_src(ui_BrewScreen_Image5, &gm_ic_thermo);
    lv_obj_set_style_img_recolor(ui_BrewScreen_Image5, GM_GOLD, 0);
    lv_obj_set_style_img_recolor_opa(ui_BrewScreen_Image5, LV_OPA_COVER, 0);
    lv_obj_align(ui_BrewScreen_Image5, LV_ALIGN_LEFT_MID, 56, 0);
    bs_track_icon(ui_BrewScreen_Image5);

    ui_BrewScreen_targetTemp = lv_label_create(ui_BrewScreen_tempContainer);
    lv_label_set_text(ui_BrewScreen_targetTemp, "93\xC2\xB0" "C");
    lv_obj_set_style_text_font(ui_BrewScreen_targetTemp, &ndot_24, 0);
    lv_obj_set_style_text_color(ui_BrewScreen_targetTemp, GM_CONTENT, 0);
    lv_obj_set_style_text_align(ui_BrewScreen_targetTemp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui_BrewScreen_targetTemp, LV_ALIGN_CENTER, 8, 0);
    bs_track_text(ui_BrewScreen_targetTemp);

    ui_BrewScreen_upTempButton = bs_glyph_btn(ui_BrewScreen_tempContainer, "+", 40,
                                              GM_RED, GM_CONTENT, true);
    lv_obj_align(ui_BrewScreen_upTempButton, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_ext_click_area(ui_BrewScreen_upTempButton, 15);
    lv_obj_add_event_cb(ui_BrewScreen_upTempButton, ui_event_BrewScreen_upTempButton,
                        LV_EVENT_ALL, NULL);

    // targetContainer: [-] [icon] [duration/weight] [+] [byTimeButton]
    // 304 wide gives 5 × 40px slots with 8px gutters. byTimeButton sits at the
    // far-right edge (only visible in volumetric mode); upDurationButton sits
    // immediately left of it so the `+` control is never occluded by the
    // delete/by-time button.
    ui_BrewScreen_targetContainer = lv_obj_create(ui_BrewScreen_adjustments);
    lv_obj_remove_style_all(ui_BrewScreen_targetContainer);
    lv_obj_set_size(ui_BrewScreen_targetContainer, 304, 54);
    lv_obj_set_style_radius(ui_BrewScreen_targetContainer, 22, 0);
    lv_obj_set_style_bg_color(ui_BrewScreen_targetContainer, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui_BrewScreen_targetContainer, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(ui_BrewScreen_targetContainer, 6, 0);
    lv_obj_clear_flag(ui_BrewScreen_targetContainer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui_BrewScreen_targetContainer, LV_OBJ_FLAG_SCROLLABLE);

    ui_BrewScreen_downDurationButton = bs_glyph_btn(ui_BrewScreen_targetContainer, "-", 40,
                                                    GM_SURFACE, GM_CONTENT, false);
    lv_obj_align(ui_BrewScreen_downDurationButton, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_ext_click_area(ui_BrewScreen_downDurationButton, 15);
    lv_obj_add_event_cb(ui_BrewScreen_downDurationButton, ui_event_BrewScreen_downDurationButton,
                        LV_EVENT_ALL, NULL);

    // Image4 toggles between time/scale glyphs at runtime via DefaultUI src swap.
    // Default: drop (volumetric), recolored to accent.
    ui_BrewScreen_Image4 = lv_img_create(ui_BrewScreen_targetContainer);
    lv_img_set_src(ui_BrewScreen_Image4, &gm_ic_drop);
    lv_obj_set_style_img_recolor(ui_BrewScreen_Image4, GM_RED, 0);
    lv_obj_set_style_img_recolor_opa(ui_BrewScreen_Image4, LV_OPA_COVER, 0);
    lv_obj_align(ui_BrewScreen_Image4, LV_ALIGN_LEFT_MID, 56, 0);
    bs_track_icon(ui_BrewScreen_Image4);

    ui_BrewScreen_targetDuration = lv_label_create(ui_BrewScreen_targetContainer);
    lv_label_set_text(ui_BrewScreen_targetDuration, "0:30");
    lv_obj_set_style_text_font(ui_BrewScreen_targetDuration, &ndot_24, 0);
    lv_obj_set_style_text_color(ui_BrewScreen_targetDuration, GM_CONTENT, 0);
    lv_obj_set_style_text_align(ui_BrewScreen_targetDuration, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui_BrewScreen_targetDuration, LV_ALIGN_CENTER, 8, 0);
    bs_track_text(ui_BrewScreen_targetDuration);

    ui_BrewScreen_upDurationButton = bs_glyph_btn(ui_BrewScreen_targetContainer, "+", 40,
                                                  GM_RED, GM_CONTENT, true);
    // Positioned one slot left of byTimeButton so the `+` control is not
    // occluded when the by-time/volumetric-delete button is shown.
    lv_obj_align(ui_BrewScreen_upDurationButton, LV_ALIGN_RIGHT_MID, -56, 0);
    lv_obj_set_ext_click_area(ui_BrewScreen_upDurationButton, 15);
    lv_obj_add_event_cb(ui_BrewScreen_upDurationButton, ui_event_BrewScreen_upDurationButton,
                        LV_EVENT_ALL, NULL);

    // byTimeButton — overlaid at right side; DefaultUI sets hidden flag based on mode.
    ui_BrewScreen_byTimeButton = lv_imgbtn_create(ui_BrewScreen_targetContainer);
    lv_imgbtn_set_src(ui_BrewScreen_byTimeButton, LV_IMGBTN_STATE_RELEASED, NULL, &gm_ic_back, NULL);
    lv_obj_set_size(ui_BrewScreen_byTimeButton, 40, 40);
    lv_obj_set_style_radius(ui_BrewScreen_byTimeButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_BrewScreen_byTimeButton, GM_RED, 0);
    lv_obj_set_style_bg_opa(ui_BrewScreen_byTimeButton, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(ui_BrewScreen_byTimeButton, GM_BG, 0);
    lv_obj_set_style_img_recolor_opa(ui_BrewScreen_byTimeButton, LV_OPA_COVER, 0);
    lv_obj_align(ui_BrewScreen_byTimeButton, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_flag(ui_BrewScreen_byTimeButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ui_BrewScreen_byTimeButton, ui_event_BrewScreen_byTimeButton,
                        LV_EVENT_ALL, NULL);
    bs_track_accent_surface(ui_BrewScreen_byTimeButton);
    bs_track_icon(ui_BrewScreen_byTimeButton);

    // ── startButton: full-width red pill at the bottom (CAR-301 chat2) ──
    //
    // Was a 64×64 round imgbtn with the cup icon. chat2 design moves to a wide
    // pill with "START SHOT" text, no leading icon. We keep the lv_imgbtn
    // widget type because:
    //   - ui_events.cpp:177 references ui_BrewScreen_startButton,
    //   - DefaultUI.cpp toggles its visibility per BrewScreenState,
    //   - imgbtn accepts child labels (LVGL renders them on top of the image
    //     slot), so adding s_start_label as a child works without changing the
    //     widget class or any of its callers.
    // We set NULL src on all states so no glyph paints behind the label.
    ui_BrewScreen_startButton = lv_imgbtn_create(ui_BrewScreen_contentPanel4);
    lv_imgbtn_set_src(ui_BrewScreen_startButton, LV_IMGBTN_STATE_RELEASED, NULL, NULL, NULL);
    lv_obj_set_size(ui_BrewScreen_startButton, 280, 56);
    lv_obj_set_style_radius(ui_BrewScreen_startButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_BrewScreen_startButton, GM_RED, 0);
    lv_obj_set_style_bg_opa(ui_BrewScreen_startButton, LV_OPA_COVER, 0);
    lv_obj_align(ui_BrewScreen_startButton, LV_ALIGN_BOTTOM_MID, 0, -54);
    lv_obj_set_ext_click_area(ui_BrewScreen_startButton, 16);
    lv_obj_add_event_cb(ui_BrewScreen_startButton, ui_event_BrewScreen_startButton,
                        LV_EVENT_ALL, NULL);
    bs_track_accent_surface(ui_BrewScreen_startButton);

    // "START SHOT" label inside the pill — centered, light-on-red.
    s_start_label = lv_label_create(ui_BrewScreen_startButton);
    lv_label_set_text(s_start_label, "START SHOT");
    lv_obj_set_style_text_font(s_start_label, &ndot_24, 0);
    // CAR-301 review (I1): hardcoded white instead of GM_BG. The startButton
    // bg is palette-tracked as an accent surface (recolored to palette.accent
    // each frame); GM_BG is the *screen* background and has no contrast
    // guarantee against an accent that may be muted in light theme. White is
    // intentionally palette-independent — the red/accent pill stays bold
    // across both themes — and matches the chat2 design's "light-on-red" cue.
    lv_obj_set_style_text_color(s_start_label, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(s_start_label, GM_TRACK_KICKER, 0);
    lv_obj_center(s_start_label);
    // NOTE: not bs_track_text() — fixed light-on-red, palette-independent.

    // ── save / accept / saveAsNew trio (Settings mode footer; default hidden) ──
    // DefaultUI's BrewScreenState reactive toggles LV_OBJ_FLAG_HIDDEN on these +
    // re-aligns on round display. They live on contentPanel4 directly so they sit
    // outside the column flex.
    //
    // CAR-325: saveButton/saveAsNewButton previously wore gm_ic_back (back chevron)
    // and gm_ic_cup (brew cup) glyphs — both misleading, and the icon set has no
    // save/disk/checkmark glyph. They are now text-label buttons ("SAVE" / "SAVE AS")
    // following the startButton pattern: keep the lv_imgbtn widget type (referenced
    // by ui_events.cpp + DefaultUI), set NULL src on all states so no glyph paints,
    // and host a centered child label. The profileDirty cue (DefaultUI effect) now
    // recolors the label text instead of the image.
    ui_BrewScreen_saveButton = lv_imgbtn_create(ui_BrewScreen_contentPanel4);
    lv_imgbtn_set_src(ui_BrewScreen_saveButton, LV_IMGBTN_STATE_RELEASED, NULL, NULL, NULL);
    lv_obj_set_size(ui_BrewScreen_saveButton, 96, 52);
    lv_obj_set_style_radius(ui_BrewScreen_saveButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_BrewScreen_saveButton, GM_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui_BrewScreen_saveButton, LV_OPA_COVER, 0);
    // CAR-326: suppress the default theme's red pressed/focused state.
    lv_obj_set_style_bg_color(ui_BrewScreen_saveButton, GM_SURFACE, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui_BrewScreen_saveButton, GM_SURFACE, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(ui_BrewScreen_saveButton, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ui_BrewScreen_saveButton, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ui_BrewScreen_saveButton, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ui_BrewScreen_saveButton, 0, LV_STATE_PRESSED);
    lv_obj_align(ui_BrewScreen_saveButton, LV_ALIGN_BOTTOM_MID, -100, -32);
    lv_obj_add_flag(ui_BrewScreen_saveButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ui_BrewScreen_saveButton, ui_event_BrewScreen_saveButton,
                        LV_EVENT_ALL, NULL);
    bs_track_button_surface(ui_BrewScreen_saveButton);

    uic_BrewScreen_save_label = lv_label_create(ui_BrewScreen_saveButton);
    lv_label_set_text(uic_BrewScreen_save_label, "SAVE");
    lv_obj_set_style_text_font(uic_BrewScreen_save_label, &grotesk_16, 0);
    lv_obj_set_style_text_color(uic_BrewScreen_save_label, GM_MUTED, 0);
    lv_obj_set_style_text_letter_space(uic_BrewScreen_save_label, GM_TRACK_KICKER, 0);
    lv_obj_center(uic_BrewScreen_save_label);
    // NOTE: not bs_track_text() — DefaultUI's profileDirty effect drives this
    // label's color (white when dirty, muted when clean).

    ui_BrewScreen_acceptButton = lv_imgbtn_create(ui_BrewScreen_contentPanel4);
    lv_imgbtn_set_src(ui_BrewScreen_acceptButton, LV_IMGBTN_STATE_RELEASED, NULL, &gm_ic_cup, NULL);
    lv_obj_set_size(ui_BrewScreen_acceptButton, 64, 64);
    lv_obj_set_style_radius(ui_BrewScreen_acceptButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_BrewScreen_acceptButton, GM_GREEN, 0);
    lv_obj_set_style_bg_opa(ui_BrewScreen_acceptButton, LV_OPA_COVER, 0);
    // CAR-326: suppress the default theme's red pressed/focused state.
    lv_obj_set_style_bg_color(ui_BrewScreen_acceptButton, GM_GREEN, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui_BrewScreen_acceptButton, GM_GREEN, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(ui_BrewScreen_acceptButton, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ui_BrewScreen_acceptButton, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ui_BrewScreen_acceptButton, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ui_BrewScreen_acceptButton, 0, LV_STATE_PRESSED);
    lv_obj_set_style_img_recolor(ui_BrewScreen_acceptButton, GM_BG, 0);
    lv_obj_set_style_img_recolor_opa(ui_BrewScreen_acceptButton, LV_OPA_COVER, 0);
    lv_obj_align(ui_BrewScreen_acceptButton, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_flag(ui_BrewScreen_acceptButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ui_BrewScreen_acceptButton, ui_event_BrewScreen_acceptButton,
                        LV_EVENT_ALL, NULL);
    bs_track_accent_surface(ui_BrewScreen_acceptButton);
    bs_track_icon(ui_BrewScreen_acceptButton);

    ui_BrewScreen_saveAsNewButton = lv_imgbtn_create(ui_BrewScreen_contentPanel4);
    lv_imgbtn_set_src(ui_BrewScreen_saveAsNewButton, LV_IMGBTN_STATE_RELEASED, NULL, NULL, NULL);
    lv_obj_set_size(ui_BrewScreen_saveAsNewButton, 96, 52);
    lv_obj_set_style_radius(ui_BrewScreen_saveAsNewButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_BrewScreen_saveAsNewButton, GM_GOLD, 0);
    lv_obj_set_style_bg_opa(ui_BrewScreen_saveAsNewButton, LV_OPA_COVER, 0);
    // CAR-326: suppress the default theme's red pressed/focused state.
    lv_obj_set_style_bg_color(ui_BrewScreen_saveAsNewButton, GM_GOLD, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui_BrewScreen_saveAsNewButton, GM_GOLD, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(ui_BrewScreen_saveAsNewButton, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ui_BrewScreen_saveAsNewButton, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ui_BrewScreen_saveAsNewButton, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ui_BrewScreen_saveAsNewButton, 0, LV_STATE_PRESSED);
    lv_obj_align(ui_BrewScreen_saveAsNewButton, LV_ALIGN_BOTTOM_MID, 100, -32);
    lv_obj_add_flag(ui_BrewScreen_saveAsNewButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ui_BrewScreen_saveAsNewButton, ui_event_BrewScreen_saveAsNewButton,
                        LV_EVENT_ALL, NULL);
    bs_track_accent_surface(ui_BrewScreen_saveAsNewButton);

    uic_BrewScreen_save_as_label = lv_label_create(ui_BrewScreen_saveAsNewButton);
    lv_label_set_text(uic_BrewScreen_save_as_label, "SAVE AS");
    lv_obj_set_style_text_font(uic_BrewScreen_save_as_label, &grotesk_16, 0);
    // Light-on-gold, palette-independent (matches START SHOT cue): the gold
    // surface is accent-tracked and stays bold across themes.
    lv_obj_set_style_text_color(uic_BrewScreen_save_as_label, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(uic_BrewScreen_save_as_label, GM_TRACK_KICKER, 0);
    lv_obj_center(uic_BrewScreen_save_as_label);
    // NOTE: not bs_track_text() — fixed light-on-gold, palette-independent.

    // Wire the screen-level event handler (gesture + screen_loaded).
    lv_obj_add_event_cb(ui_BrewScreen, ui_event_BrewScreen, LV_EVENT_ALL, NULL);
}

void ui_BrewScreen_screen_destroy(void) {
    if (ui_BrewScreen)
        lv_obj_del(ui_BrewScreen);

    // NULL all SquareLine globals so DefaultUI's lv_obj_is_valid() guards trip
    // correctly between destroy and the next init.
    ui_BrewScreen = NULL;
    ui_BrewScreen_dials = NULL;
    ui_BrewScreen_ImgButton5 = NULL;
    ui_BrewScreen_contentPanel4 = NULL;
    ui_BrewScreen_mainLabel3 = NULL;
    ui_BrewScreen_startButton = NULL;
    ui_BrewScreen_controlContainer = NULL;
    ui_BrewScreen_modeSwitch = NULL;
    ui_BrewScreen_volumetricButton = NULL;
    ui_BrewScreen_weightLabel = NULL;
    ui_BrewScreen_profileInfo = NULL;
    ui_BrewScreen_Label1 = NULL;
    ui_BrewScreen_Container3 = NULL;
    ui_BrewScreen_profileSelectBtn = NULL;
    ui_BrewScreen_profileName = NULL;
    ui_BrewScreen_settingsButton = NULL;
    ui_BrewScreen_adjustments = NULL;
    ui_BrewScreen_tempContainer = NULL;
    ui_BrewScreen_targetTemp = NULL;
    ui_BrewScreen_downTempButton = NULL;
    ui_BrewScreen_upTempButton = NULL;
    ui_BrewScreen_Image5 = NULL;
    ui_BrewScreen_targetContainer = NULL;
    ui_BrewScreen_targetDuration = NULL;
    ui_BrewScreen_upDurationButton = NULL;
    ui_BrewScreen_downDurationButton = NULL;
    ui_BrewScreen_Image4 = NULL;
    ui_BrewScreen_byTimeButton = NULL;
    ui_BrewScreen_saveButton = NULL;
    ui_BrewScreen_acceptButton = NULL;
    ui_BrewScreen_saveAsNewButton = NULL;

    // Dials children (custom UI components)
    uic_BrewScreen_dials_tempText = NULL;
    uic_BrewScreen_dials_pressureText = NULL;
    uic_BrewScreen_dials_pressureTarget = NULL;
    uic_BrewScreen_dials_pressureGauge = NULL;
    uic_BrewScreen_dials_tempTarget = NULL;
    uic_BrewScreen_dials_tempGauge = NULL;

    // Status bar handle (owned by this screen while active).
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
    // teardown paths). bs_status_time always tracks the label this screen
    // owns; when the global has moved on, leave it alone. CAR-297 (mirrors
    // the CAR-294 GrindScreen guarded-clobber fix).
    if (gm_h.status_time == bs_status_time) {
        gm_h.status_time = NULL;
    }
    bs_status_time = NULL;
    bs_status_bar = NULL;

    // Theme-tracking arrays
    bs_text_count = 0;
    bs_recolor_icon_count = 0;
    bs_button_surface_count = 0;
    bs_accent_surface_count = 0;
    bs_kicker_label = NULL;
    uic_BrewScreen_hero_value = NULL;
    uic_BrewScreen_hero_unit = NULL;
    uic_BrewScreen_status_pill = NULL;
    uic_BrewScreen_ratio_sub = NULL;
    uic_BrewScreen_save_label = NULL;    // CAR-325
    uic_BrewScreen_save_as_label = NULL; // CAR-325
    s_start_label = NULL;
    for (int i = 0; i < (int)(sizeof(bs_text_labels) / sizeof(bs_text_labels[0])); i++)
        bs_text_labels[i] = NULL;
    for (int i = 0; i < (int)(sizeof(bs_recolor_icons) / sizeof(bs_recolor_icons[0])); i++)
        bs_recolor_icons[i] = NULL;
    for (int i = 0; i < (int)(sizeof(bs_button_surfaces) / sizeof(bs_button_surfaces[0])); i++)
        bs_button_surfaces[i] = NULL;
    for (int i = 0; i < (int)(sizeof(bs_accent_surfaces) / sizeof(bs_accent_surfaces[0])); i++)
        bs_accent_surfaces[i] = NULL;
}

// CAR-293 / CAR-279 pattern: recolor children with active palette so the
// screen stays legible on UI_THEME_LIGHT (where styleScreenBase repaints the
// bg from GM_BG black to a near-white surface).
//   text         - primary text (labels, stepper glyphs, profileName, weight)
//   muted        - kicker tone fallback + secondary captions
//   buttonSurface- minus / save / settings button bg
//   accent       - plus / start / accept / saveAsNew / profileSelect bg
//   kickerTone   - mainLabel3 tone (matches ringVisual.tone — DefaultUI
//                  overrides this immediately after, but pass it through here
//                  so a theme switch alone stays consistent if DefaultUI's
//                  ring update doesn't fire).
void ui_BrewScreen_apply_palette(lv_color_t text, lv_color_t muted,
                                 lv_color_t buttonSurface, lv_color_t accent,
                                 lv_color_t kickerTone) {
    if (bs_kicker_label != NULL && lv_obj_is_valid(bs_kicker_label)) {
        lv_obj_set_style_text_color(bs_kicker_label, kickerTone, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    for (int i = 0; i < bs_text_count; i++) {
        lv_obj_t *o = bs_text_labels[i];
        if (o != NULL && lv_obj_is_valid(o)) {
            lv_obj_set_style_text_color(o, text, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    for (int i = 0; i < bs_recolor_icon_count; i++) {
        lv_obj_t *o = bs_recolor_icons[i];
        if (o != NULL && lv_obj_is_valid(o)) {
            lv_obj_set_style_img_recolor(o, text, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor_opa(o, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    for (int i = 0; i < bs_button_surface_count; i++) {
        lv_obj_t *o = bs_button_surfaces[i];
        if (o != NULL && lv_obj_is_valid(o)) {
            lv_obj_set_style_bg_color(o, buttonSurface, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    for (int i = 0; i < bs_accent_surface_count; i++) {
        lv_obj_t *o = bs_accent_surfaces[i];
        if (o != NULL && lv_obj_is_valid(o)) {
            lv_obj_set_style_bg_color(o, accent, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    // Label1 (secondary caption) reads better in muted tone; override the
    // primary-text recolor we applied above. It's the last entry only when no
    // ensureBrewContextLabel sibling is in the way; safe to recolor by handle.
    if (ui_BrewScreen_Label1 != NULL && lv_obj_is_valid(ui_BrewScreen_Label1)) {
        lv_obj_set_style_text_color(ui_BrewScreen_Label1, muted, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // Recolor the gm_status_bar() children via the shared helper. The
    // class-detection walk + rationale lives in gm_ui.cpp
    // (gm_status_bar_apply_palette / CAR-295).
    gm_status_bar_apply_palette(bs_status_bar, text, muted);
}
