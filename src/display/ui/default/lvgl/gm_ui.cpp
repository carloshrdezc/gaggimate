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

gm_handles_t gm_h;

// Place a recolored icon. The gm_ic_* masters are 40px; keep the object
// at that native size so lv_img_set_zoom stays centered, then scale to
// target_px. Tint via img_recolor (ALPHA_8BIT draws entirely in it).
static lv_obj_t *gm_icon(lv_obj_t *parent, const lv_img_dsc_t *src, lv_color_t color, int target_px) {
    lv_obj_t *im = lv_img_create(parent);
    lv_img_set_src(im, src);
    lv_obj_set_size(im, 40, 40);
    lv_img_set_zoom(im, (uint16_t)(256 * target_px / 40));
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

    gm_icon(bar, &gm_ic_wifi, GM_MUTED, 16);
    gm_icon(bar, &gm_ic_bt, GM_MUTED, 16);

    // Placeholder until the screen's update hook sets the real device clock.
    gm_h.status_time = lv_label_create(bar);
    lv_label_set_text(gm_h.status_time, "--:--");
    lv_obj_set_style_text_font(gm_h.status_time, &spacemono_14, 0);
    lv_obj_set_style_text_color(gm_h.status_time, GM_CONTENT, 0);

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
        bool on = (i == active);
        if (on) {
            lv_obj_set_style_bg_color(chip, accent, 0);
            lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        }
        lv_obj_t *ic = gm_icon(chip, ICON[i], on ? GM_BG : GM_MUTED, 28);
        lv_obj_center(ic);
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
    int active_chip = -1;
    if (mode == 1) active_chip = 1;
    else if (mode == 2) active_chip = 2;
    else if (mode == 3) active_chip = 3;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *chip = gm_h.chips[i];
        if (!chip) continue;
        const bool on = (i == active_chip);
        lv_obj_set_style_bg_color(chip, accent, 0);
        lv_obj_set_style_bg_opa(chip, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        // The chip's icon is its first (and only) child; recolor it so the
        // active chip's glyph reads against the filled accent.
        lv_obj_t *icon = lv_obj_get_child(chip, 0);
        if (icon) {
            lv_obj_set_style_img_recolor(icon, on ? GM_BG : GM_MUTED, 0);
        }
    }

    // Per-mode visibility.
    switch (mode) {
        case 2: { // steam — only temp metric, arc shows target, READY pill on completion.
            gm_show(gm_h.arc, true);
            gm_show(gm_h.bar, false);
            gm_show(gm_h.m_weight, false);
            gm_show(gm_h.m_temp, true);
            gm_show(gm_h.m_press, false);
            gm_show(gm_h.m_flow, false);
            gm_show(gm_h.w_target, false);
            gm_show(gm_h.w_temp, false);
            gm_show(gm_h.w_flow, false);
            gm_show(gm_h.pill, bar_pct >= 100);
            break;
        }
        case 3: { // water — w_target/w_temp/w_flow, arc hidden, bar visible.
            gm_show(gm_h.arc, false);
            gm_show(gm_h.bar, true);
            gm_show(gm_h.m_weight, false);
            gm_show(gm_h.m_temp, false);
            gm_show(gm_h.m_press, false);
            gm_show(gm_h.m_flow, false);
            gm_show(gm_h.w_target, true);
            gm_show(gm_h.w_temp, true);
            gm_show(gm_h.w_flow, true);
            gm_show(gm_h.pill, false);
            break;
        }
        case 1:
        default: { // brew (and fallback) — full metric row, arc, no bar/pill.
            gm_show(gm_h.arc, true);
            gm_show(gm_h.bar, false);
            gm_show(gm_h.m_weight, true);
            gm_show(gm_h.m_temp, true);
            gm_show(gm_h.m_press, true);
            gm_show(gm_h.m_flow, true);
            gm_show(gm_h.w_target, false);
            gm_show(gm_h.w_temp, false);
            gm_show(gm_h.w_flow, false);
            gm_show(gm_h.pill, false);
            break;
        }
    }
}
