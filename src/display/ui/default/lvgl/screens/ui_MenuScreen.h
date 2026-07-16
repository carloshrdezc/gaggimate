// CAR-279: ui_MenuScreen rebuilt as the Nothing-theme Quick-settings list.
// The mode hub (4 tiles + standby) lives in ui_ModeScreen now. Only the
// screen symbol name is preserved (other code reaches us via this name).

#ifndef UI_MENUSCREEN_H
#define UI_MENUSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_MenuScreen_screen_init(void);
extern void ui_MenuScreen_screen_destroy(void);
extern void ui_event_MenuScreen(lv_event_t *e);
// CAR-279 review fix: re-color the Quick-settings children to track the
// active palette (text/muted/buttonSurface/accent). Called from the
// ui_MenuScreen branch of DefaultUI::applyScreenVisualLanguage().
extern void ui_MenuScreen_apply_palette(lv_color_t text, lv_color_t muted, lv_color_t buttonSurface, lv_color_t accent);

extern lv_obj_t *ui_MenuScreen;
extern lv_obj_t *ui_MenuScreen_contentPanel;
extern lv_obj_t *ui_MenuScreen_backButton;
extern lv_obj_t *ui_MenuScreen_doneButton; // CAR-358: full-width exit
extern lv_obj_t *ui_MenuScreen_restartButton;
extern lv_obj_t *ui_MenuScreen_brightnessSwitch;
extern lv_obj_t *ui_MenuScreen_brewTempValue;
extern lv_obj_t *ui_MenuScreen_brewTempMinus;
extern lv_obj_t *ui_MenuScreen_brewTempPlus;
// CAR-358: water + steam temp steppers.
extern lv_obj_t *ui_MenuScreen_waterTempValue;
extern lv_obj_t *ui_MenuScreen_waterTempMinus;
extern lv_obj_t *ui_MenuScreen_waterTempPlus;
extern lv_obj_t *ui_MenuScreen_steamTempValue;
extern lv_obj_t *ui_MenuScreen_steamTempMinus;
extern lv_obj_t *ui_MenuScreen_steamTempPlus;
extern lv_obj_t *ui_MenuScreen_scaleSwitch;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // UI_MENUSCREEN_H
