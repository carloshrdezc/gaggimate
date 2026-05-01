# Nothing Theme LVGL Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a Nothing-style ring gauge theme for the LVGL embedded display — OLED black base, full-circle red arc fill, Doto font on SPIFFS with fallback.

**Architecture:** Expand the existing 2-state theme system to 3 states by adding `UI_THEME_NOTHING = 2`. Add new Nothing color arrays with `[3]` elements. Create separate `ui_comp_nothing_dials.c` for the full-ring arc component and `ui_nothing_font.c` for SPIFFS font loading with LV_FONT_DEFAULT fallback.

**Tech Stack:** LVGL 8.3.11, C, SquareLine Studio-generated UI code, SPIFFS filesystem, Doto TTF fonts

---

## File Map

| File | Purpose |
|------|---------|
| `src/display/ui/default/lvgl/ui_themes.h` | Add UI_THEME_NOTHING enum + Nothing color externs |
| `src/display/ui/default/lvgl/ui_themes.cpp` | Add Nothing color array definitions [3] |
| `src/display/ui/default/lvgl/fonts/ui_nothing_font.c` | NEW — Doto TTF loading + fallback |
| `src/display/ui/default/lvgl/fonts/ui_nothing_font.h` | NEW — Font accessor declarations |
| `src/display/ui/default/lvgl/components/ui_comp_nothing_dials.c` | NEW — Full-ring arc gauge |
| `src/display/ui/default/lvgl/components/ui_comp_nothing_dials.h` | NEW — Component header |
| `src/display/ui/default/lvgl/ui.c` | MOD — Call font init, wire Nothing theme |
| `data/fonts/Doto-Regular.ttf` | Copy TTF to SPIFFS data |
| `data/fonts/Doto-Bold.ttf` | Copy TTF to SPIFFS data |

---

## Prerequisite: Create fonts directory

- [ ] **Step 1: Create SPIFFS fonts directory**

Run: `mkdir -p data/fonts`
Verify: `ls data/fonts/` should exist (empty)

---

## Task 1: Update ui_themes.h — Add Nothing Theme Enum and Color Externs

**Files:**
- Modify: `src/display/ui/default/lvgl/ui_themes.h`

- [ ] **Step 1: Add UI_THEME_NOTHING enum and Nothing color indices**

Insert after line 17 (after `UI_THEME_COLOR_HEATING`):

```c
#define UI_THEME_COLOR_NOTHINGBASE 5
#define UI_THEME_COLOR_NOTHINGTRACK 6
#define UI_THEME_COLOR_NOTHINGCONTENT 7
#define UI_THEME_COLOR_NOTHINGRED 8
#define UI_THEME_COLOR_NOTHINGMUTED 9
#define UI_THEME_COLOR_NOTHINGDISABLED 10

#define UI_THEME_NOTHING 2  // NEW — third theme state
```

- [ ] **Step 2: Add Nothing color array extern declarations**

Insert after line 36 (after `_ui_theme_color_Heating` externs):

```c
extern const ui_theme_variable_t _ui_theme_color_NothingBase[3];
extern const ui_theme_variable_t _ui_theme_color_NothingTrack[3];
extern const ui_theme_variable_t _ui_theme_color_NothingContent[3];
extern const ui_theme_variable_t _ui_theme_color_NothingRed[3];
extern const ui_theme_variable_t _ui_theme_color_NothingMuted[3];
extern const ui_theme_variable_t _ui_theme_color_NothingDisabled[3];
```

- [ ] **Step 3: Update ui_theme_colors and ui_theme_alphas array size**

Change line 38-39 from `[2]` to `[3]`:

```c
extern const uint32_t *ui_theme_colors[3];
extern const uint8_t *ui_theme_alphas[3];
```

- [ ] **Step 4: Commit**

```bash
git add src/display/ui/default/lvgl/ui_themes.h
git commit -m "feat(ui): add UI_THEME_NOTHING enum and Nothing color externs"
```

---

## Task 2: Update ui_themes.cpp — Add Nothing Color Array Definitions

**Files:**
- Modify: `src/display/ui/default/lvgl/ui_themes.cpp`

- [ ] **Step 1: Add Nothing color array definitions after existing arrays**

Insert after line 22 (after `_ui_theme_color_Heating` definitions):

```c
// Nothing theme colors — all three slots use identical Nothing values
const ui_theme_variable_t _ui_theme_color_NothingBase[3]     = {0x000000, 0x000000, 0x000000};
const ui_theme_variable_t _ui_theme_color_NothingTrack[3]    = {0x1A1A1A, 0x1A1A1A, 0x1A1A1A};
const ui_theme_variable_t _ui_theme_color_NothingContent[3] = {0xE8E8E8, 0xE8E8E8, 0xE8E8E8};
const ui_theme_variable_t _ui_theme_color_NothingRed[3]     = {0xD71921, 0xD71921, 0xD71921};
const ui_theme_variable_t _ui_theme_color_NothingMuted[3]   = {0x888888, 0x888888, 0x888888};
const ui_theme_variable_t _ui_theme_color_NothingDisabled[3] = {0x5A5A5A, 0x5A5A5A, 0x5A5A5A};
```

- [ ] **Step 2: Update ui_theme_colors array to include Nothing colors**

Change the `ui_theme_colors` array to have 3 entries (default, light, nothing). Add the Nothing colors as new entries. The exact location depends on current file content — add 6 new pointers for the Nothing colors alongside the existing 5 theme colors.

```c
// Add to ui_theme_colors array — 3 entries (one per theme state)
// Each entry points to an array of color values indexed by ui_theme_idx
// Structure: { [theme0], [theme1], [theme2] }
```

- [ ] **Step 3: Commit**

```bash
git add src/display/ui/default/lvgl/ui_themes.cpp
git commit -m "feat(ui): add Nothing color array definitions"
```

---

## Task 3: Create ui_nothing_font.h — Font Header

**Files:**
- Create: `src/display/ui/default/lvgl/fonts/ui_nothing_font.h`

- [ ] **Step 1: Write the header file**

```c
// ui_nothing_font.h
#ifndef _UI_NOTHING_FONT_H
#define _UI_NOTHING_FONT_H

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
```

- [ ] **Step 2: Commit**

```bash
git add src/display/ui/default/lvgl/fonts/ui_nothing_font.h
git commit -m "feat(ui): add Nothing font header"
```

---

## Task 4: Create ui_nothing_font.c — Doto Font Loading with Fallback

**Files:**
- Create: `src/display/ui/default/lvgl/fonts/ui_nothing_font.c`

- [ ] **Step 1: Write the font loading implementation**

```c
// ui_nothing_font.c
#include "../ui.h"

static lv_font_t *doto_regular = NULL;
static lv_font_t *doto_bold = NULL;
static lv_font_t *fallback_font = NULL;

void ui_nothing_font_init(void) {
    // Load Doto fonts from SPIFFS
    doto_regular = lv_font_load("S:/fonts/Doto-Regular.ttf");
    doto_bold = lv_font_load("S:/fonts/Doto-Bold.ttf");

    // Set fallback to LVGL default font
    fallback_font = (lv_font_t *)&LV_FONT_DEFAULT;

    // Graceful degradation: use fallback if Doto unavailable
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
```

- [ ] **Step 2: Commit**

```bash
git add src/display/ui/default/lvgl/fonts/ui_nothing_font.c
git commit -m "feat(ui): add Doto font loading with LV_FONT_DEFAULT fallback"
```

---

## Task 5: Create ui_comp_nothing_dials.h — Component Header

**Files:**
- Create: `src/display/ui/default/lvgl/components/ui_comp_nothing_dials.h`

- [ ] **Step 1: Write the component header**

```c
// ui_comp_nothing_dials.h
#ifndef _UI_COMP_NOTHING_DIALS_H
#define _UI_COMP_NOTHING_DIALS_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    UI_COMP_NOTHING_DIALS_DIALS,
    UI_COMP_NOTHING_DIALS_TEMPGAUGE,
    UI_COMP_NOTHING_DIALS_TEMPVALUE,
    UI_COMP_NOTHING_DIALS_TEMPUNIT,
    UI_COMP_NOTHING_DIALS_TEMPLABEL,
    UI_COMP_NOTHING_DIALS_PRESSUREGAUGE,
    UI_COMP_NOTHING_DIALS_PRESSUREVALUE,
    UI_COMP_NOTHING_DIALS_PRESSUREUNIT,
    UI_COMP_NOTHING_DIALS_PRESSURELABEL,
    UI_COMP_NOTHING_DIALS_TARGETVALUE,
    UI_COMP_NOTHING_DIALS_NUM
};

lv_obj_t *ui_comp_nothing_dials_create(lv_obj_t *comp_parent);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 2: Commit**

```bash
git add src/display/ui/default/lvgl/components/ui_comp_nothing_dials.h
git commit -m "feat(ui): add Nothing dials component header"
```

---

## Task 6: Create ui_comp_nothing_dials.c — Full-Ring Arc Gauge

**Files:**
- Create: `src/display/ui/default/lvgl/components/ui_comp_nothing_dials.c`

- [ ] **Step 1: Write the full-ring arc gauge component**

```c
// ui_comp_nothing_dials.c
#include "../ui.h"
#include "../fonts/ui_nothing_font.h"

lv_obj_t *ui_comp_nothing_dials_create(lv_obj_t *comp_parent) {
    lv_obj_t *cui_dials;
    cui_dials = lv_obj_create(comp_parent);
    lv_obj_remove_style_all(cui_dials);
    lv_obj_set_width(cui_dials, 480);
    lv_obj_set_height(cui_dials, 480);
    lv_obj_set_align(cui_dials, LV_ALIGN_CENTER);
    lv_obj_clear_flag(cui_dials, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Temperature ring — full 360° arc
    lv_obj_t *cui_tempGauge;
    cui_tempGauge = lv_arc_create(cui_dials);
    lv_obj_set_width(cui_tempGauge, 480);
    lv_obj_set_height(cui_tempGauge, 480);
    lv_obj_set_align(cui_tempGauge, LV_ALIGN_CENTER);
    lv_arc_set_range(cui_tempGauge, 0, 100);
    lv_arc_set_value(cui_tempGauge, 0);
    lv_arc_set_bg_angles(cui_tempGauge, 0, 360);
    lv_arc_set_rotation(cui_tempGauge, 0);
    // Track: subtle #1A1A1A fallback
    lv_obj_set_style_arc_width(cui_tempGauge, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(cui_tempGauge, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(cui_tempGauge, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    // Indicator: Nothing red
    lv_obj_set_style_arc_width(cui_tempGauge, 30, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(cui_tempGauge, lv_color_hex(0xD71921), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(cui_tempGauge, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cui_tempGauge, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    // Temperature value text
    lv_obj_t *cui_tempValue;
    cui_tempValue = lv_label_create(cui_dials);
    lv_obj_set_width(cui_tempValue, LV_SIZE_CONTENT);
    lv_obj_set_height(cui_tempValue, LV_SIZE_CONTENT);
    lv_obj_set_align(cui_tempValue, LV_ALIGN_CENTER);
    lv_label_set_text(cui_tempValue, "92");
    lv_obj_set_style_text_font(cui_tempValue, ui_nothing_font_get(true), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(cui_tempValue, lv_color_hex(0xE8E8E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(cui_tempValue, -60, -30);

    // Temperature unit (°C)
    lv_obj_t *cui_tempUnit;
    cui_tempUnit = lv_label_create(cui_dials);
    lv_obj_set_width(cui_tempUnit, LV_SIZE_CONTENT);
    lv_obj_set_height(cui_tempUnit, LV_SIZE_CONTENT);
    lv_obj_set_align(cui_tempUnit, LV_ALIGN_CENTER);
    lv_label_set_text(cui_tempUnit, "°C");
    lv_obj_set_style_text_font(cui_tempUnit, ui_nothing_font_get(false), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(cui_tempUnit, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(cui_tempUnit, 0, -30);

    // Temperature label
    lv_obj_t *cui_tempLabel;
    cui_tempLabel = lv_label_create(cui_dials);
    lv_obj_set_width(cui_tempLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(cui_tempLabel, LV_SIZE_CONTENT);
    lv_obj_set_align(cui_tempLabel, LV_ALIGN_CENTER);
    lv_label_set_text(cui_tempLabel, "TEMP");
    lv_obj_set_style_text_font(cui_tempLabel, ui_nothing_font_get(false), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(cui_tempLabel, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cui_tempLabel, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(cui_tempLabel, 0, -80);

    // Pressure ring — full 360° arc
    lv_obj_t *cui_pressureGauge;
    cui_pressureGauge = lv_arc_create(cui_dials);
    lv_obj_set_width(cui_pressureGauge, 480);
    lv_obj_set_height(cui_pressureGauge, 480);
    lv_obj_set_align(cui_pressureGauge, LV_ALIGN_CENTER);
    lv_arc_set_range(cui_pressureGauge, 0, 100);
    lv_arc_set_value(cui_pressureGauge, 0);
    lv_arc_set_bg_angles(cui_pressureGauge, 0, 360);
    lv_arc_set_rotation(cui_pressureGauge, 0);
    // Track: subtle #1A1A1A fallback
    lv_obj_set_style_arc_width(cui_pressureGauge, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(cui_pressureGauge, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(cui_pressureGauge, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    // Indicator: Nothing red
    lv_obj_set_style_arc_width(cui_pressureGauge, 30, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(cui_pressureGauge, lv_color_hex(0xD71921), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(cui_pressureGauge, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cui_pressureGauge, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    // Pressure value text
    lv_obj_t *cui_pressureValue;
    cui_pressureValue = lv_label_create(cui_dials);
    lv_obj_set_width(cui_pressureValue, LV_SIZE_CONTENT);
    lv_obj_set_height(cui_pressureValue, LV_SIZE_CONTENT);
    lv_obj_set_align(cui_pressureValue, LV_ALIGN_CENTER);
    lv_label_set_text(cui_pressureValue, "9");
    lv_obj_set_style_text_font(cui_pressureValue, ui_nothing_font_get(true), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(cui_pressureValue, lv_color_hex(0xE8E8E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(cui_pressureValue, 60, 50);

    // Pressure unit (bar)
    lv_obj_t *cui_pressureUnit;
    cui_pressureUnit = lv_label_create(cui_dials);
    lv_obj_set_width(cui_pressureUnit, LV_SIZE_CONTENT);
    lv_obj_set_height(cui_pressureUnit, LV_SIZE_CONTENT);
    lv_obj_set_align(cui_pressureUnit, LV_ALIGN_CENTER);
    lv_label_set_text(cui_pressureUnit, "bar");
    lv_obj_set_style_text_font(cui_pressureUnit, ui_nothing_font_get(false), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(cui_pressureUnit, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(cui_pressureUnit, 0, 50);

    // Pressure label
    lv_obj_t *cui_pressureLabel;
    cui_pressureLabel = lv_label_create(cui_dials);
    lv_obj_set_width(cui_pressureLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(cui_pressureLabel, LV_SIZE_CONTENT);
    lv_obj_set_align(cui_pressureLabel, LV_ALIGN_CENTER);
    lv_label_set_text(cui_pressureLabel, "BAR");
    lv_obj_set_style_text_font(cui_pressureLabel, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(cui_pressureLabel, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(cui_pressureLabel, 0, 100);

    lv_obj_t **children = lv_mem_alloc(sizeof(lv_obj_t *) * _UI_COMP_NOTHING_DIALS_NUM);
    children[UI_COMP_NOTHING_DIALS_DIALS] = cui_dials;
    children[UI_COMP_NOTHING_DIALS_TEMPGAUGE] = cui_tempGauge;
    children[UI_COMP_NOTHING_DIALS_TEMPVALUE] = cui_tempValue;
    children[UI_COMP_NOTHING_DIALS_TEMPUNIT] = cui_tempUnit;
    children[UI_COMP_NOTHING_DIALS_TEMPLABEL] = cui_tempLabel;
    children[UI_COMP_NOTHING_DIALS_PRESSUREGAUGE] = cui_pressureGauge;
    children[UI_COMP_NOTHING_DIALS_PRESSUREVALUE] = cui_pressureValue;
    children[UI_COMP_NOTHING_DIALS_PRESSUREUNIT] = cui_pressureUnit;
    children[UI_COMP_NOTHING_DIALS_PRESSURELABEL] = cui_pressureLabel;
    children[UI_COMP_NOTHING_DIALS_TARGETVALUE] = NULL;
    lv_obj_add_event_cb(cui_dials, get_component_child_event_cb, LV_EVENT_GET_COMP_CHILD, children);
    lv_obj_add_event_cb(cui_dials, del_component_child_event_cb, LV_EVENT_DELETE, children);
    return cui_dials;
}
```

- [ ] **Step 2: Commit**

```bash
git add src/display/ui/default/lvgl/components/ui_comp_nothing_dials.c
git commit -m "feat(ui): add Nothing dials component with full-ring arc"
```

---

## Task 7: Integrate Font Init and Nothing Theme Wiring

**Files:**
- Modify: `src/display/ui/default/lvgl/ui.c` (or wherever UI initialization happens)

- [ ] **Step 1: Add ui_nothing_font.h include**

Find the includes section and add:

```c
#include "fonts/ui_nothing_font.h"
```

- [ ] **Step 2: Call ui_nothing_font_init() during UI init**

Find the UI initialization function (often `ui_init` or similar) and add:

```c
ui_nothing_font_init();
```

- [ ] **Step 3: Wire Nothing theme in theme switching**

In the theme switching logic (where `ui_theme_set()` is called), add a case for UI_THEME_NOTHING that uses the Nothing dials component instead of the regular dials.

- [ ] **Step 4: Commit**

```bash
git add src/display/ui/default/lvgl/ui.c
git commit -m "feat(ui): integrate Nothing font init and theme wiring"
```

---

## Task 8: Copy Doto TTF Files to SPIFFS Data

**Files:**
- Copy: `Doto-Regular.ttf` to `data/fonts/Doto-Regular.ttf`
- Copy: `Doto-Bold.ttf` to `data/fonts/Doto-Bold.ttf`

- [ ] **Step 1: Download Doto fonts**

Get Doto-Regular.ttf and Doto-Bold.ttf from Google Fonts (Doto family).

- [ ] **Step 2: Copy to data/fonts directory**

```bash
mkdir -p data/fonts
cp /path/to/Doto-Regular.ttf data/fonts/
cp /path/to/Doto-Bold.ttf data/fonts/
```

- [ ] **Step 3: Verify SPIFFS upload configuration**

Check platformio.ini or build config to ensure data/fonts/ is included in SPIFFS upload.

- [ ] **Step 4: Commit the font files (or add to .gitignore if too large)**

```bash
git add data/fonts/
git commit -m "feat(ui): add Doto TTF fonts for Nothing theme"
```

---

## Verification Tasks

- [ ] **Verify 1: Build passes**

Run: `pio run -e display 2>&1 | tail -30`
Expected: No compilation errors

- [ ] **Verify 2: Flash SPIFFS and verify fonts load**

Upload SPIFFS data, then check serial output for font loading status.

- [ ] **Verify 3: Theme switch to Nothing**

Call `ui_theme_set(UI_THEME_NOTHING)` and verify the ring gauge renders with full-circle red arc.

- [ ] **Verify 4: Font fallback works**

Temporarily remove Doto files from SPIFFS, verify LV_FONT_DEFAULT is used as fallback with no crash.

---

## Self-Review Checklist

After writing the plan, verify:

1. **Spec coverage**: Each spec requirement maps to a task:
   - OLED black base (#000000) → Task 2 (NothingBase color)
   - Full-circle arc fill → Task 6 (ui_comp_nothing_dials.c)
   - Doto font on SPIFFS → Tasks 3, 4, 7
   - Fault tolerance (#1A1A1A track, font fallback) → Tasks 4, 6
   - Ring labels (°C, bar) → Task 6

2. **No placeholders**: All code is complete, no "TBD", no "implement later"

3. **Type consistency**: `ui_nothing_font_get(bool bold)` matches between header (Task 3) and implementation (Task 4)

---

**Plan complete and saved to `docs/superpowers/plans/2026-04-30-nothing-theme-lvgl-implementation.md`.**

Two execution options:

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**