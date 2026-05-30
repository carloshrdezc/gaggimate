// ─────────────────────────────────────────────────────────────
//  gm_ui.h — shared builders + live-update handles for the
//  GaggiMate "Nothing" round-display theme.
//
//  Foundation layer (CAR-276): the reusable widget builders the
//  screens (CAR-277/278/279) assemble. Ported to this repo's
//  LVGL v8 from the v9 design reference.
// ─────────────────────────────────────────────────────────────
#ifndef GM_UI_H
#define GM_UI_H

#include "gm_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Live-update handles ──────────────────────────────────────
// Populated during screen build so update fns can poke values
// without re-walking the tree every frame.
typedef struct {
    lv_obj_t *clock;        // standby hero (ndot_120, digits+colon only)
    lv_obj_t *ampm;         // standby AM/PM suffix (spacemono_14, hidden in 24h mode)
    lv_obj_t *standby_temp; // standby sub
    lv_obj_t *status_label; // STANDBY · READY
    lv_obj_t *status_time;  // status-bar clock (set live from the update hooks)

    lv_obj_t *arc;          // status edge arc
    lv_obj_t *kicker;       // INFUSION / DISPENSING / context
    lv_obj_t *hero;         // big numeral
    lv_obj_t *hero_unit;    // ° / g suffix
    lv_obj_t *m_weight, *m_temp, *m_press, *m_flow; // brew metrics
    lv_obj_t *w_target, *w_temp, *w_flow;           // water metrics
    lv_obj_t *pill;         // steam READY pill
    lv_obj_t *bar;          // linear progress
    lv_obj_t *chips[4];     // mode chip fills
} gm_handles_t;

extern gm_handles_t gm_h;

// ── Shared builders (gm_ui.cpp) ──────────────────────────────
lv_obj_t *gm_make_screen(void);
lv_obj_t *gm_kicker(lv_obj_t *parent, const char *txt, lv_color_t col);
lv_obj_t *gm_edge_arc(lv_obj_t *parent, lv_color_t accent);
lv_obj_t *gm_status_bar(lv_obj_t *parent, bool live);
lv_obj_t *gm_chip_bar(lv_obj_t *parent, int active, lv_color_t accent);
lv_obj_t *gm_metric(lv_obj_t *row, const char *label, const char *value, lv_color_t value_col);
lv_obj_t *gm_progress(lv_obj_t *parent, lv_color_t accent);

// Update an existing metric column's label text. `value` is the handle returned
// by gm_metric() (the value label); the column label is its sibling.
// No-op if value is null/invalid.
void gm_metric_set_label(lv_obj_t *value, const char *new_label);

// ── Status screen mode switch (CAR-278) ──────────────────────
// Retints accents and shows/hides the appropriate widgets per brewing mode.
//   mode: 1 = brew (red), 2 = steam (gold), 3 = water (blue); other values
//         fall back to neutral content. Matches MODE_* in core/constants.h
//         so callers can pass the controller mode directly.
//   arc_pct, bar_pct: 0-100 progress values for the edge arc and the
//         linear progress bar. The arc is hidden in water mode; the bar
//         is shown in water mode and used as the steam target indicator.
void gm_status_apply_mode(int mode, int arc_pct, int bar_pct);

#ifdef __cplusplus
}
#endif

#endif // GM_UI_H
