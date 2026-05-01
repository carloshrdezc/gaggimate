// ui_nothing_font.h
#ifndef _UI_NOTHING_FONT_H
#define _UI_NOTHING_FONT_H

#include "../ui.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_nothing_font_init(void);
lv_font_t *ui_nothing_font_get(bool bold);
bool ui_nothing_font_is_loaded(void);

#ifdef __cplusplus
}
#endif

#endif
