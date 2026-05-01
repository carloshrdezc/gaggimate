// ui_comp_nothing_dials.c
#include "ui_comp_nothing_dials.h"
#include "../ui_themes.h"
#include "../fonts/ui_nothing_font.h"
#include "lvgl.h"

#define NOTHING_RED     0xD71921
#define NOTHING_TRACK   0x1A1A1A
#define NOTHING_CONTENT 0xE8E8E8
#define NOTHING_MUTED   0x888888

lv_obj_t *ui_nothing_dials_create(lv_obj_t *comp_parent) {
    const lv_font_t *doto_regular = ui_nothing_font_get(false);
    const lv_font_t *doto_bold = ui_nothing_font_get(true);
    const lv_font_t *font_value = (doto_bold != (const lv_font_t *)&lv_font_montserrat_14) ? doto_bold : &lv_font_montserrat_24;
    const lv_font_t *font_unit = (doto_regular != (const lv_font_t *)&lv_font_montserrat_14) ? doto_regular : &lv_font_montserrat_14;
    const lv_font_t *font_target = &lv_font_montserrat_14;

    // --- Temperature Ring (left half) ---
    lv_obj_t *temp_gauge = lv_arc_create(comp_parent);
    lv_arc_set_bg_angles(temp_gauge, 0, 360);
    lv_arc_set_range(temp_gauge, 0, 100);
    lv_arc_set_value(temp_gauge, 0);
    lv_obj_set_size(temp_gauge, 180, 180);
    lv_obj_set_pos(temp_gauge, 50, 150);
    lv_obj_set_style_arc_width(temp_gauge, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(temp_gauge, lv_color_hex(NOTHING_TRACK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(temp_gauge, lv_color_hex(NOTHING_RED), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(temp_gauge, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_arc_set_rotation(temp_gauge, 0);

    lv_obj_t *temp_value = lv_label_create(comp_parent);
    lv_label_set_text(temp_value, "93");
    lv_obj_set_style_text_font(temp_value, font_value, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(temp_value, lv_color_hex(NOTHING_CONTENT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(temp_value, 65, 195);

    lv_obj_t *temp_unit = lv_label_create(comp_parent);
    lv_label_set_text(temp_unit, "°C");
    lv_obj_set_style_text_font(temp_unit, font_unit, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(temp_unit, lv_color_hex(NOTHING_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(temp_unit, 65, 230);

    // --- Pressure Ring (right half) ---
    lv_obj_t *pressure_gauge = lv_arc_create(comp_parent);
    lv_arc_set_bg_angles(pressure_gauge, 0, 360);
    lv_arc_set_range(pressure_gauge, 0, 100);
    lv_arc_set_value(pressure_gauge, 0);
    lv_obj_set_size(pressure_gauge, 180, 180);
    lv_obj_set_pos(pressure_gauge, 250, 150);
    lv_obj_set_style_arc_width(pressure_gauge, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(pressure_gauge, lv_color_hex(NOTHING_TRACK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(pressure_gauge, lv_color_hex(NOTHING_RED), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(pressure_gauge, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_arc_set_rotation(pressure_gauge, 0);

    lv_obj_t *pressure_value = lv_label_create(comp_parent);
    lv_label_set_text(pressure_value, "9.0");
    lv_obj_set_style_text_font(pressure_value, font_value, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(pressure_value, lv_color_hex(NOTHING_CONTENT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(pressure_value, 265, 195);

    lv_obj_t *pressure_unit = lv_label_create(comp_parent);
    lv_label_set_text(pressure_unit, "bar");
    lv_obj_set_style_text_font(pressure_unit, font_unit, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(pressure_unit, lv_color_hex(NOTHING_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(pressure_unit, 265, 230);

    // --- Target Value (centered) ---
    lv_obj_t *target_value = lv_label_create(comp_parent);
    lv_label_set_text(target_value, "Target: --");
    lv_obj_set_style_text_font(target_value, font_target, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(target_value, lv_color_hex(NOTHING_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(target_value, 170, 400);

    return NULL;
}