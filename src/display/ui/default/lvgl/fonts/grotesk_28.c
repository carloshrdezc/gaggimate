/*******************************************************************************
 * Size: 28 px
 * Bpp: 4
 * Opts: --bpp 4 --no-compress --format lvgl --lv-include lvgl.h --force-fast-kern-format --font assets/fonts/SpaceGrotesk-Medium.ttf --size 28 --range 0x2B,0x2D -o src/display/ui/default/lvgl/fonts/grotesk_28.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef GROTESK_28
#define GROTESK_28 1
#endif

#if GROTESK_28

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+002B "+" */
    0x0, 0x0, 0x5, 0x88, 0x0, 0x0, 0x0, 0x0,
    0x0, 0xaf, 0xf0, 0x0, 0x0, 0x0, 0x0, 0xa,
    0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0xaf, 0xf0,
    0x0, 0x0, 0x0, 0x0, 0xa, 0xff, 0x0, 0x0,
    0x2, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x2f,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x81, 0x99, 0x99,
    0xdf, 0xf9, 0x99, 0x95, 0x0, 0x0, 0xa, 0xff,
    0x0, 0x0, 0x0, 0x0, 0x0, 0xaf, 0xf0, 0x0,
    0x0, 0x0, 0x0, 0xa, 0xff, 0x0, 0x0, 0x0,
    0x0, 0x0, 0xaf, 0xf0, 0x0, 0x0,

    /* U+002D "-" */
    0x89, 0x99, 0x99, 0x99, 0x3e, 0xff, 0xff, 0xff,
    0xf5, 0xef, 0xff, 0xff, 0xff, 0x50
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 278, .box_w = 13, .box_h = 12, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 78, .adv_w = 198, .box_w = 9, .box_h = 3, .ofs_x = 2, .ofs_y = 6}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint8_t glyph_id_ofs_list_0[] = {
    0, 0, 1
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 43, .range_length = 3, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = glyph_id_ofs_list_0, .list_length = 3, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t grotesk_28 = {
#else
lv_font_t grotesk_28 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 12,          /*The maximum line height required by the font*/
    .base_line = -4,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -3,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if GROTESK_28*/

