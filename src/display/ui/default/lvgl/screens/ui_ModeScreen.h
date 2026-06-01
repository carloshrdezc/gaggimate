// GaggiMate "Nothing" theme — Mode launcher screen (CAR-291).
// Hand-written (not SquareLine). Mirrors the old ui_MenuScreen mode-hub
// role: 4 tiles (brew/steam/water/grind) + standby + settings entry.
// Settings entry navigates to ui_MenuScreen, which is now the
// Quick-settings list (CAR-279) rather than a tile hub.

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
extern lv_obj_t *ui_ModeScreen_dials;
extern lv_obj_t *ui_ModeScreen_contentPanel1;
extern lv_obj_t *ui_ModeScreen_btnBrew;
extern lv_obj_t *ui_ModeScreen_btnSteam;
extern lv_obj_t *ui_ModeScreen_waterBtn;
extern lv_obj_t *ui_ModeScreen_grindBtn;
extern lv_obj_t *ui_ModeScreen_standbyButton;
extern lv_obj_t *ui_ModeScreen_settingsButton;

// Dial child component handles (parity with the old ui_MenuScreen
// uic_MenuScreen_dials_* names so DefaultUI's reactive blocks can be
// re-targeted with a name-only swap).
extern lv_obj_t *uic_ModeScreen_dials_tempGauge;
extern lv_obj_t *uic_ModeScreen_dials_tempTarget;
extern lv_obj_t *uic_ModeScreen_dials_pressureGauge;
extern lv_obj_t *uic_ModeScreen_dials_pressureTarget;
extern lv_obj_t *uic_ModeScreen_dials_pressureText;
extern lv_obj_t *uic_ModeScreen_dials_tempText;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // UI_MODESCREEN_H
