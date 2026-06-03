// GaggiMate "Nothing" theme — Mode launcher screen (CAR-308).
// Hand-written (not SquareLine). 2×2 mode-tile grid (Brew/Steam/Water/Grind)
// + bottom Standby pill + top-right Settings round icon. Replaces the old
// SquareLine-themed mode hub. The screen owns its visuals via the gm_*
// builders + GM_* tokens; DefaultUI no longer restyles it from
// applyScreenPalette().

#ifndef UI_MODESCREEN_H
#define UI_MODESCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_ModeScreen
extern void ui_ModeScreen_screen_init(void);
extern void ui_ModeScreen_screen_destroy(void);
extern void ui_event_ModeScreen(lv_event_t *e);

extern lv_obj_t *ui_ModeScreen;
extern lv_obj_t *ui_ModeScreen_contentPanel1;
extern lv_obj_t *ui_ModeScreen_btnBrew;
extern lv_obj_t *ui_ModeScreen_btnSteam;
extern lv_obj_t *ui_ModeScreen_waterBtn;
extern lv_obj_t *ui_ModeScreen_grindBtn;
extern lv_obj_t *ui_ModeScreen_standbyButton;
extern lv_obj_t *ui_ModeScreen_settingsButton;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // UI_MODESCREEN_H
