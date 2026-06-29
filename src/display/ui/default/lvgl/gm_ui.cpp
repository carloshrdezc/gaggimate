// ─────────────────────────────────────────────────────────────
//  gm_ui.cpp — GaggiMate "Nothing" round-display theme
//  Shared builders (foundation, CAR-276). The screens that use
//  these (standby / status / quick-settings) are built in
//  CAR-277/278/279. Ported from the v9 design reference to v8:
//   - lv_obj_set_style_pad_hor -> pad_left + pad_right (no v8 helper)
//   - status bar / chips use the CAR-273 A8 image icons (gm_ic_*)
//     recolored at runtime, rather than a symbol font.
// ─────────────────────────────────────────────────────────────
#include "gm_ui.h"
#include "gm_icons.h"

// CAR-321: WiFi/BT status icons. Our custom gm_ic_* assets are LV_IMG_CF_ALPHA_8BIT
// (alpha-only) and render INVISIBLE in the status bar on this AMOLED panel at every
// size/technique tried (CAR-302/307/309/314/321 — zoom, flex, native 22px, native
// 40px all failed). The UPSTREAM SquareLine wifi/bt assets are LV_IMG_CF_TRUE_COLOR_ALPHA
// 20x20 and are PROVEN to render on this exact hardware (original StandbyScreen). Reuse
// them directly here instead of fighting the A8 path.
// NOTE: LV_IMG_DECLARE expands to a plain `extern const lv_img_dsc_t`, which in a
// C++ TU gives these symbols C++ linkage and mangled names. The assets are defined
// in images/ui_img_*.c as C symbols, so we MUST wrap the declarations in extern "C"
// to reference the un-mangled names and link successfully (CAR-321 review).
extern "C" {
LV_IMG_DECLARE(ui_img_364513079);  // wifi-20x20.png  (TRUE_COLOR_ALPHA)
LV_IMG_DECLARE(ui_img_1091371356); // bluetooth-alt-20x20.png (TRUE_COLOR_ALPHA)
}

gm_handles_t gm_h;

// ─────────────────────────────────────────────────────────────
//  gm_icon — downscaled recolored icon (LVGL 8.4 idiom)
// ─────────────────────────────────────────────────────────────
//
// The gm_ic_* masters are 40px ALPHA_8BIT (CAR-273); we render them at
// smaller target sizes via zoom + recolor. The canonical v8 way to do
// this is REAL size mode + zoom, with NO manual size and NO pivot tweaks.
//
// Five regressions on this helper so far — DO NOT "improve" without
// re-reading lvgl/src/widgets/lv_img.c::draw_img first:
//   * CAR-302 introduced REAL+zoom+pivot(0,0)+set_size — drifted up-left.
//   * CAR-307 dropped pivot(0,0) → default center → still wrong combo,
//     icons rendered then disappeared in some layouts.
//   * CAR-309 dropped set_size: REAL mode was assumed to auto-size to the
//     transformed area — but it does NOT (no call after set_src refreshes
//     the cached self-size), so SIZE_CONTENT parents laid the icon out at
//     stale 40px and the draw tiled/clipped.
//   * CAR-314 re-added lv_obj_set_size(im, target, target) to fix the
//     layout box — but that was the REGRESSION: REAL-mode draw assumes
//     object size == transformed size. Pinning a different box (e.g. 14
//     while img->w stays 40) breaks the centering shift (img->w-transformed)/2
//     and clips the downscaled alpha mask outside the box → invisible icons.
//
// RESOLUTION (CAR-316): keep REAL mode, REMOVE the CAR-314 set_size pin, and
// call lv_obj_refresh_self_size(im) right AFTER set_zoom. set_src is the only
// thing that refreshes the cached self-size, and at that point zoom is still
// NONE + mode VIRTUAL, so the widget caches its 40px native size; neither
// set_zoom nor set_size_mode re-refresh it (see lvgl/src/widgets/lv_img.c).
// Refreshing self-size after the zoom makes the cached self-size equal the
// transformed (zoomed) size, so flex / SIZE_CONTENT parents lay out the
// correct box AND object == transformed keeps the REAL-mode draw centered —
// exactly why the native-40px ModeScreen tiles (no zoom, no set_size) render.
//
// Rules for this helper (LVGL 8.4):
//   * DO NOT re-pin the size with lv_obj_set_size(...) — that was CAR-314's
//     regression. refresh_self_size after set_zoom is what fixes the box.
//   * DO NOT call lv_img_set_pivot(...) — the REAL-mode draw path in
//     lv_img.c (≈line 621-624 of lv_img.c::draw_img) uses the default
//     centered pivot (w/2, h/2) for its area-compensation math; moving
//     the pivot breaks that compensation and the icon clips or vanishes.
//   * Source masters are 40px → zoom factor is 256 * target_px / 40
//     (LVGL zoom unit: 256 = 1.0×).
//   * Tint is via img_recolor at LV_OPA_COVER — ALPHA_8BIT sources draw
//     entirely in the recolor.
__attribute__((unused)) static lv_obj_t *gm_icon(lv_obj_t *parent, const lv_img_dsc_t *src, lv_color_t color, int target_px) {
    lv_obj_t *im = lv_img_create(parent);
    lv_img_set_src(im, src);                                 // 40px native; pivot defaults to (20,20)
    lv_img_set_zoom(im, (uint16_t)(256 * target_px / 40));   // map 40 → target_px
    lv_obj_refresh_self_size(im);                            // cache transformed (zoomed) size as self-size
    lv_img_set_size_mode(im, LV_IMG_SIZE_MODE_REAL);         // draw the transformed (zoomed) bitmap
    // NO lv_obj_set_size — REAL-mode draw assumes object == transformed size;
    // re-pinning the box (CAR-314) clips the downscaled mask. NO lv_img_set_pivot —
    // default centered pivot is what REAL-mode draw expects.
    lv_obj_set_style_img_recolor(im, color, 0);
    lv_obj_set_style_img_recolor_opa(im, LV_OPA_COVER, 0);
    return im;
}

// ─────────────────────────────────────────────────────────────
//  SHARED BUILDERS
// ─────────────────────────────────────────────────────────────

lv_obj_t *gm_make_screen(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, GM_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    return scr;
}

lv_obj_t *gm_kicker(lv_obj_t *parent, const char *txt, lv_color_t col) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &spacemono_14, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_letter_space(l, GM_TRACK_KICKER, 0);
    return l;
}

// Thin progress/ramp arc hugging the bezel. 270° sweep, opens at bottom.
lv_obj_t *gm_edge_arc(lv_obj_t *parent, lv_color_t accent) {
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 464, 464);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, GM_FAINT, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}

// Top status bar: wifi + bt icons, time, and a live dot when active.
lv_obj_t *gm_status_bar(lv_obj_t *parent, bool live) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 14, 0);

    // CAR-321: draw WiFi/BT from the upstream TRUE_COLOR_ALPHA 20x20 assets
    // (proven to render on this panel). Our custom gm_ic_* assets are
    // LV_IMG_CF_ALPHA_8BIT and render invisible in the status bar on this AMOLED
    // panel at every size/technique tried (CAR-302/307/309/314/321). recolor->
    // GM_CONTENT keeps them on-theme. They stay as direct lv_img children of the
    // bar so gm_status_bar_apply_palette()'s class-walk still recolors them on
    // theme flips.
    {
        const lv_img_dsc_t *SICON[2] = {&ui_img_364513079, &ui_img_1091371356};
        for (int i = 0; i < 2; i++) {
            lv_obj_t *ic = lv_img_create(bar);
            lv_img_set_src(ic, SICON[i]);
            lv_obj_set_style_img_recolor(ic, GM_CONTENT, 0);
            lv_obj_set_style_img_recolor_opa(ic, LV_OPA_COVER, 0);
        }
    }

    // Placeholder until the screen's update hook sets the real device clock.
    // CAR-315: start hidden so the "--:--" placeholder never renders (the dash
    // glyph reads as missing-glyph rectangles on some screens). The shared
    // DefaultUI::updateStatusBarClock() un-hides it once a valid time exists,
    // mirroring the CAR-299/CAR-300 StandbyScreen placeholder handling.
    gm_h.status_time = lv_label_create(bar);
    lv_label_set_text(gm_h.status_time, "--:--");
    lv_obj_set_style_text_font(gm_h.status_time, &spacemono_14, 0);
    lv_obj_set_style_text_color(gm_h.status_time, GM_CONTENT, 0);
    lv_obj_add_flag(gm_h.status_time, LV_OBJ_FLAG_HIDDEN);

    if (live) {
        lv_obj_t *dot = lv_obj_create(bar);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, GM_GREEN, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    }
    return bar;
}

// Bottom 4-chip mode bar. `active` highlights one chip with the accent;
// its icon knocks out to the background color over the fill.
lv_obj_t *gm_chip_bar(lv_obj_t *parent, int active, lv_color_t accent) {
    const lv_img_dsc_t *ICON[4] = {&gm_ic_power, &gm_ic_cup, &gm_ic_steam, &gm_ic_drop};

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, 6, 0);
    lv_obj_set_style_pad_all(bar, 6, 0);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, GM_CONTENT, 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_10, 0);

    for (int i = 0; i < 4; i++) {
        lv_obj_t *chip = lv_obj_create(bar);
        lv_obj_remove_style_all(chip);
        lv_obj_set_size(chip, 44, 44);
        lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        bool on = (i == active);
        if (on) {
            lv_obj_set_style_bg_color(chip, accent, 0);
            lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        }
        // Icon rendering follows the PROVEN ui_ModeScreen build_mode_tile() recipe
        // verbatim: native REAL size mode (NO zoom), recolor, then lv_obj_align()
        // on a NON-flex parent. The 40px A8 masters fit the 44px chip at native
        // size. Earlier attempts failed because: (a) gm_icon() zooms then the
        // flex/center re-pinned the box and collapsed the downscaled mask
        // ("5 regressions" warning on gm_icon), and (b) putting a flex layout on
        // the chip re-measures the REAL-mode child and clips it the same way.
        // Keep the chip layout-free and align the icon directly — same as the
        // ModeScreen tiles, which render correctly on hardware.
        lv_obj_t *ic = lv_img_create(chip);
        lv_img_set_src(ic, ICON[i]);
        lv_obj_set_style_img_recolor(ic, on ? GM_BG : GM_CONTENT, 0);
        lv_obj_set_style_img_recolor_opa(ic, on ? LV_OPA_COVER : LV_OPA_70, 0);
        lv_img_set_size_mode(ic, LV_IMG_SIZE_MODE_REAL);
        lv_obj_align(ic, LV_ALIGN_CENTER, 0, 0);
        gm_h.chips[i] = chip;
    }
    return bar;
}

// One metric column (label over value). Add several to a flex row.
lv_obj_t *gm_metric(lv_obj_t *row, const char *label, const char *value, lv_color_t value_col) {
    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(col, 13, 0);
    lv_obj_set_style_pad_right(col, 13, 0);
    lv_obj_set_style_pad_row(col, 4, 0);

    lv_obj_t *l = lv_label_create(col);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &spacemono_11, 0);
    lv_obj_set_style_text_color(l, GM_FAINT, 0);
    lv_obj_set_style_text_letter_space(l, GM_TRACK_LABEL, 0);

    lv_obj_t *v = lv_label_create(col);
    lv_label_set_text(v, value);
    lv_obj_set_style_text_font(v, &ndot_28, 0);
    lv_obj_set_style_text_color(v, value_col, 0);

    return v; // return the value label so callers can store the handle
}

// Update an existing metric column's label text. The metric column built by
// gm_metric() above has child 0 = label, child 1 = value. `value` is the
// handle returned by gm_metric(); we walk to its parent column then to the
// label sibling.
void gm_metric_set_label(lv_obj_t *value, const char *new_label) {
    if (value == nullptr || !lv_obj_is_valid(value)) return;
    lv_obj_t *col = lv_obj_get_parent(value);
    if (col == nullptr) return;
    lv_obj_t *label = lv_obj_get_child(col, 0);
    if (label != nullptr && lv_obj_is_valid(label)) {
        lv_label_set_text(label, new_label);
    }
}

void gm_metric_show(lv_obj_t *value, bool visible) {
    if (value == nullptr || !lv_obj_is_valid(value)) return;
    lv_obj_t *col = lv_obj_get_parent(value);
    if (col == nullptr || !lv_obj_is_valid(col)) return;
    if (visible) {
        lv_obj_clear_flag(col, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(col, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *gm_progress(lv_obj_t *parent, lv_color_t accent) {
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 220, 4);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, GM_CONTENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, accent, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    return bar;
}

// ─────────────────────────────────────────────────────────────
//  STATUS SCREEN MODE SWITCH (CAR-278)
//
//  The status screen is built once in brew layout. This switch retints
//  the accent surfaces and hides/shows the per-mode widget set so the
//  same screen serves brew/steam/water without rebuilding.
// ─────────────────────────────────────────────────────────────

// Saturate a percentage to the inclusive [0, 100] range. Takes int so callers
// must convert from float themselves and clamp NaN before calling — pattern is
// `arcPct = static_cast<int>(...)` in DefaultUI.cpp::updateStatusScreen, where
// the upstream divisor is already guarded to be non-zero. Do not re-introduce
// NaN handling here; if a callsite produces float, use isfinite() at that
// callsite the way clampPercent() does in DefaultUI.cpp.
static int gm_clamp_pct(int v) {
    if (v < 0) return 0;
    if (v > 100) return 100;
    return v;
}

static void gm_show(lv_obj_t *o, bool visible) {
    if (!o) return;
    if (visible) {
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

void gm_status_apply_mode(int mode, int arc_pct, int bar_pct) {
    const lv_color_t accent = gm_accent_for_mode(mode);
    arc_pct = gm_clamp_pct(arc_pct);
    bar_pct = gm_clamp_pct(bar_pct);

    // Retint shared accent surfaces.
    if (gm_h.arc) {
        lv_obj_set_style_arc_color(gm_h.arc, accent, LV_PART_INDICATOR);
        lv_arc_set_value(gm_h.arc, arc_pct);
    }
    if (gm_h.kicker) {
        lv_obj_set_style_text_color(gm_h.kicker, accent, 0);
    }
    if (gm_h.hero_unit) {
        lv_obj_set_style_text_color(gm_h.hero_unit, accent, 0);
    }
    if (gm_h.bar) {
        lv_obj_set_style_bg_color(gm_h.bar, accent, LV_PART_INDICATOR);
        lv_bar_set_value(gm_h.bar, bar_pct, LV_ANIM_OFF);
    }
    // Chip bar: highlight the chip for the active mode (1=cup, 2=steam,
    // 3=drop). Index 0 is the standby chip, kept dim.
    // Modes 1/2/3 light their own chip; 0 (standby) and 4 (grind) leave
    // all chips dim (active_chip stays -1).
    const int active_chip = (mode >= 1 && mode <= 3) ? mode : -1;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *chip = gm_h.chips[i];
        if (!chip) continue;
        const bool on = (i == active_chip);
        lv_obj_set_style_bg_color(chip, accent, 0);
        lv_obj_set_style_bg_opa(chip, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        // The chip's icon is its first (and only) child; recolor it so the
        // active chip's glyph reads against the filled accent.
        lv_obj_t *icon = lv_obj_get_child(chip, 0);
        if (icon != nullptr && lv_obj_is_valid(icon)) {
            lv_obj_set_style_img_recolor(icon, on ? GM_BG : GM_MUTED, 0);
        }
    }

    // Per-mode visibility.
    switch (mode) {
        case 2: { // steam — only temp metric, arc shows target, READY pill on completion.
            gm_show(gm_h.arc, true);
            gm_show(gm_h.bar, false);
            gm_metric_show(gm_h.m_weight, false);
            gm_metric_show(gm_h.m_temp, true);
            // CAR-278 review #6: hero shows current temp; the metric column
            // below it shows the target, so relabel TEMP → TARGET to match.
            gm_metric_set_label(gm_h.m_temp, "TARGET");
            gm_metric_show(gm_h.m_press, false);
            gm_metric_show(gm_h.m_flow, false);
            gm_metric_show(gm_h.w_target, false);
            gm_metric_show(gm_h.w_temp, false);
            gm_metric_show(gm_h.w_flow, false);
            // CAR-358: suppress the READY pill in steam — the steppers occupy
            // the y=100 band; at-target is shown by the filled arc + green icon.
            gm_show(gm_h.pill, false);
            // CAR-358: steam temp steppers shown only here; water hint hidden.
            gm_show(gm_h.steam_steppers, true);
            gm_show(gm_h.water_hint, false);
            break;
        }
        case 3: { // water — w_target/w_temp/w_flow, arc hidden, bar visible.
            gm_show(gm_h.arc, false);
            gm_show(gm_h.bar, true);
            gm_metric_show(gm_h.m_weight, false);
            gm_metric_show(gm_h.m_temp, false);
            gm_metric_show(gm_h.m_press, false);
            gm_metric_show(gm_h.m_flow, false);
            gm_metric_show(gm_h.w_target, true);
            gm_metric_show(gm_h.w_temp, true);
            gm_metric_show(gm_h.w_flow, true);
            gm_show(gm_h.pill, false);
            // CAR-358: water tap hint shown only here; steam steppers hidden.
            gm_show(gm_h.steam_steppers, false);
            gm_show(gm_h.water_hint, true);
            break;
        }
        case 1:
        default: { // brew (and fallback) — full metric row, linear bar, no arc/pill.
            // mode==0 (standby) and mode>=4 (grind) fall here defensively. In
            // practice updateStatusScreen() only runs when ui_StatusScreen is
            // active, so those values shouldn't reach this helper — but if a
            // future code path dispatches StatusScreen for grind, the brew
            // layout is the closest sensible default.
            // CAR-300: arc is for steam/water; brew uses the linear bar to show
            // dose progress (volumetric fill or phase time as a bottom anchor).
            gm_show(gm_h.arc, false);
            gm_show(gm_h.bar, true);
            gm_metric_show(gm_h.m_weight, true);
            gm_metric_show(gm_h.m_temp, true);
            // CAR-278 review #6: restore TEMP label after switching back from
            // steam (which relabels this column to TARGET).
            gm_metric_set_label(gm_h.m_temp, "TEMP");
            gm_metric_show(gm_h.m_press, true);
            gm_metric_show(gm_h.m_flow, true);
            gm_metric_show(gm_h.w_target, false);
            gm_metric_show(gm_h.w_temp, false);
            gm_metric_show(gm_h.w_flow, false);
            gm_show(gm_h.pill, false);
            // CAR-358: steam steppers + water hint are steam/water-only; hide
            // both in brew (and the standby/grind fallback).
            gm_show(gm_h.steam_steppers, false);
            gm_show(gm_h.water_hint, false);
            break;
        }
    }
}

// ── gm_status_bar_apply_palette (CAR-295) ────────────────────
// Centralized recolor for status-bar children. Owning screens cache the
// bar handle at build time and call this from their apply_palette() when
// the theme flips. The walk identifies children by LVGL class rather
// than by pointer equality with gm_h.status_time: handleScreenChange()
// runs _ui_screen_change (which builds the new screen and re-points
// gm_h.status_time at the *new* clock) BEFORE lv_obj_del(current)
// destroys the previous screen. The previous screen's destroy hook then
// NULLs gm_h.status_time, clobbering any freshly-stored handle. Pointer
// comparison would miss the clock and leave it at hard-coded near-white
// on light backgrounds (CAR-294 / CAR-297). The status bar only owns
// one label child (wifi/bt are images, live-dot is a bare lv_obj with a
// bg_color), so class detection is unambiguous. Live-dot green is a
// semantic accent, not a theme tone — leave it alone.
void gm_status_bar_apply_palette(lv_obj_t *bar, lv_color_t text, lv_color_t muted) {
    if (bar == NULL || !lv_obj_is_valid(bar)) return;
    const uint32_t child_count = lv_obj_get_child_cnt(bar);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(bar, i);
        if (child == NULL || !lv_obj_is_valid(child)) continue;
        if (lv_obj_check_type(child, &lv_label_class)) {
            // Clock label — only label child of the status bar.
            lv_obj_set_style_text_color(child, text, LV_PART_MAIN | LV_STATE_DEFAULT);
        } else if (lv_obj_check_type(child, &lv_img_class)) {
            // Wifi / bt status icons.
            lv_obj_set_style_img_recolor(child, muted, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor_opa(child, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        // Live-dot is an lv_obj with a bg_color (not image, not label) — skip.
    }
}
