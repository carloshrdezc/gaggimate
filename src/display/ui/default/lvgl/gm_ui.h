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
    lv_obj_t *clock;        // standby hero
    lv_obj_t *standby_temp; // standby sub
    lv_obj_t *status_label; // STANDBY · READY

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

#ifdef __cplusplus
}
#endif

#endif // GM_UI_H
