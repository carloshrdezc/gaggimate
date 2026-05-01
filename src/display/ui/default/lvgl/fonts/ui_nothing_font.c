// ui_nothing_font.c
#include "ui_nothing_font.h"
#include "lvgl.h"

static lv_font_t *doto_regular = NULL;
static lv_font_t *doto_bold = NULL;
static lv_font_t *fallback_font = NULL;

void ui_nothing_font_init(void) {
    doto_regular = lv_font_load("S:/fonts/Doto-Regular.ttf");
    doto_bold = lv_font_load("S:/fonts/Doto-Bold.ttf");

    fallback_font = (lv_font_t *)&lv_font_montserrat_14;
    if (doto_regular == NULL) {
        doto_regular = fallback_font;
    }
    if (doto_bold == NULL) {
        doto_bold = fallback_font;
    }
}

lv_font_t *ui_nothing_font_get(bool bold) {
    return bold ? doto_bold : doto_regular;
}

bool ui_nothing_font_is_loaded(void) {
    return (doto_regular != fallback_font) || (doto_bold != fallback_font);
}