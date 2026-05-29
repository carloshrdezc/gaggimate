// ─────────────────────────────────────────────────────────────
//  gm_theme.h — GaggiMate "Nothing" display theme
//  Palette + per-mode accent for the 480×480 round LVGL UI.
//  Single source of truth: include this anywhere you build a screen.
//  Fonts live in gm_fonts.h (generated via assets/fonts/gen_fonts.sh).
// ─────────────────────────────────────────────────────────────
#ifndef GM_THEME_H
#define GM_THEME_H

#include "gm_fonts.h"

// ── Palette ──────────────────────────────────────────────────
// LVGL takes 24-bit hex and converts to the panel's RGB565 itself.
#define GM_BG       lv_color_hex(0x000000) // OLED black base
#define GM_SURFACE  lv_color_hex(0x060606) // chips / card fill
#define GM_CONTENT  lv_color_hex(0xE8E8E8) // primary text + numerals
#define GM_MUTED    lv_color_hex(0x8A8A8A) // labels / secondary
#define GM_FAINT    lv_color_hex(0x4A4A4A) // ticks / tertiary
#define GM_RED      lv_color_hex(0xD71921) // brew / brand accent
#define GM_HEAT     lv_color_hex(0xF0561D) // heating indicator
#define GM_GOLD     lv_color_hex(0xF5C44A) // steam mode accent
#define GM_BLUE     lv_color_hex(0x6699CC) // water mode accent
#define GM_GREEN    lv_color_hex(0x7CB876) // ready / OK state

// ── Per-mode accent lookup ───────────────────────────────────
// 1 = brew, 2 = steam, 3 = water; anything else = neutral content.
static inline lv_color_t gm_accent_for_mode(int mode) {
    switch (mode) {
        case 1:  return GM_RED;     // brew
        case 2:  return GM_GOLD;    // steam
        case 3:  return GM_BLUE;    // water
        default: return GM_CONTENT; // standby / neutral
    }
}

// Letter-spacing helpers (px) tuned to approximate the web tracking.
#define GM_TRACK_KICKER 4 // ~0.30em @ 14px
#define GM_TRACK_LABEL  2 // ~0.18em @ 11px

#endif // GM_THEME_H
