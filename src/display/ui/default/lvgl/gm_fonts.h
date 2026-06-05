// ─────────────────────────────────────────────────────────────
//  gm_fonts.h — GaggiMate "Nothing" theme font declarations
//
//  Generated with lv_font_conv (see assets/fonts/gen_fonts.sh):
//    ndot_*      from web/public/fonts/Ndot57Caps-Regular.otf  (dot-matrix numerals)
//    spacemono_* from assets/fonts/SpaceMono-Bold.ttf          (kickers / labels)
//    grotesk_16  from assets/fonts/SpaceGrotesk-Medium.ttf     (body / settings)
//
//  Glyph ranges are subset per font to keep flash small; regenerate with
//  `bash assets/fonts/gen_fonts.sh` if you need more glyphs.
// ─────────────────────────────────────────────────────────────
#ifndef GM_FONTS_H
#define GM_FONTS_H

#if defined __has_include
#if __has_include("lvgl.h")
#include "lvgl.h"
#elif __has_include("lvgl/lvgl.h")
#include "lvgl/lvgl.h"
#else
#include "lvgl.h"
#endif
#else
#include "lvgl.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(ndot_150);     // hero numerals: brew timer, steam/water hero
LV_FONT_DECLARE(ndot_120);     // standby clock
LV_FONT_DECLARE(ndot_60);      // hero unit suffix (° g s C)
LV_FONT_DECLARE(ndot_28);      // metric values
LV_FONT_DECLARE(spacemono_14); // kickers / status labels
LV_FONT_DECLARE(spacemono_11); // small metric labels
LV_FONT_DECLARE(grotesk_16);   // body / settings rows
LV_FONT_DECLARE(grotesk_28);   // +/- stepper glyphs (subset: 0x2B, 0x2D)

#ifdef __cplusplus
}
#endif

#endif // GM_FONTS_H
