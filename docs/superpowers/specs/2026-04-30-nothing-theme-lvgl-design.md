# Nothing Theme for LVGL Embedded Display — Design Specification

**Date:** 2026-04-30
**Status:** Approved for implementation

---

## 1. Overview

Implement a Nothing-style theme for the LVGL embedded display, bringing the signature Nothing UI ring gauge aesthetic from the web UI to the physical device. The focus of this spec is the ring/dial gauge component — the most visually distinctive element of the Nothing design language.

**Goals:**
- OLED black base with Nothing red (#d71921) accent
- Full-circle arc fill for the ring gauge (value fills the entire 360° ring)
- Doto font for display values (loaded from SPIFFS)
- Minimal, high-contrast minimalist aesthetic

---

## 2. Theme Architecture

### 2.1 Theme Index Expansion

Expand the existing 2-state theme system to 3 states:

```c
// ui_themes.h
#define UI_THEME_DEFAULT 0
#define UI_THEME_LIGHT   1
#define UI_THEME_NOTHING  2  // NEW — must match array index in theme_color arrays
```

### 2.2 Color Palette

| Name | Hex | Usage |
|------|-----|-------|
| NothingBase | #000000 | OLED black — background |
| NothingTrack | #1A1A1A | Fallback track — slightly visible for fault tolerance |
| NothingContent | #E8E8E8 | Light gray — primary text |
| NothingRed | #D71921 | Nothing brand red — indicator fill, accents |
| NothingMuted | #888888 | Mid gray — secondary text, labels |
| NothingDisabled | #5A5A5A | Dark gray — disabled states (WCAG AA contrast on black) |

### 2.3 New Theme Color Arrays

```c
// ui_themes.cpp — add after existing arrays
// All three slots (default, light, nothing) use Nothing values for nothing theme
const ui_theme_variable_t _ui_theme_color_NothingBase[3]     = {0x000000, 0x000000, 0x000000};
const ui_theme_variable_t _ui_theme_color_NothingTrack[3]    = {0x1A1A1A, 0x1A1A1A, 0x1A1A1A};  // Fallback visible track
const ui_theme_variable_t _ui_theme_color_NothingContent[3] = {0xE8E8E8, 0xE8E8E8, 0xE8E8E8};  // Consistent cool gray
const ui_theme_variable_t _ui_theme_color_NothingRed[3]     = {0xD71921, 0xD71921, 0xD71921};
const ui_theme_variable_t _ui_theme_color_NothingMuted[3]   = {0x888888, 0x888888, 0x888888};
const ui_theme_variable_t _ui_theme_color_NothingDisabled[3] = {0x5A5A5A, 0x5A5A5A, 0x5A5A5A};  // WCAG AA on black
```

Note: The `[3]` array stores `[default_value, light_value, nothing_value]`. When `ui_theme_idx = 2`, `ui_get_theme_value()` returns the third element. All three slots now use identical Nothing values for consistency.

---

## 3. Font Integration

### 3.1 Doto Font on SPIFFS

**Files needed:**
- `Doto-Regular.ttf` → SPIFFS `/fonts/Doto-Regular.ttf`
- `Doto-Bold.ttf` → SPIFFS `/fonts/Doto-Bold.ttf`

**Note:** Doto does not have a separate Mono variant. Use Doto Regular for monospace-style labels by applying letter-spacing via LVGL's style system, or use the LVGL built-in monospace font as fallback for mono labels.

### 3.2 Loading at Runtime

```c
// ui_nothing_font.cpp
static lv_font_t *doto_regular = NULL;
static lv_font_t *doto_bold = NULL;
static lv_font_t *fallback_font = NULL;

void ui_nothing_font_init(void) {
    // Load Doto fonts from SPIFFS
    doto_regular = lv_font_load("S:/fonts/Doto-Regular.ttf");
    doto_bold = lv_font_load("S:/fonts/Doto-Bold.ttf");

    // Fallback to guaranteed built-in font if Doto fails
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
    // Check if Doto loaded successfully (not using fallback)
    return (doto_regular != fallback_font) || (doto_bold != fallback_font);
}
```

Call `ui_nothing_font_init()` during UI initialization, before theme switch. If Doto is unavailable, the interface degrades gracefully to the built-in font.

---

## 4. Ring Gauge Component

### 4.1 Current Implementation

The existing `ui_comp_dials.c` creates two `lv_arc` objects:
- `cui_tempGauge` — temperature gauge, range 0-160, arc span 124° (118° to 242°)
- `cui_pressureGauge` — pressure gauge, range 0-16, arc span 124° (298° to 62°)

### 4.2 Nothing Ring Design

**Ring characteristics:**

| Property | Value | Notes |
|----------|-------|-------|
| Track | #1A1A1A | Subtle fallback visible track for fault tolerance |
| Indicator | Full 360° arc | Nothing red (#d71921) fills clockwise from top |
| Arc width | 30px | Slightly thinner than current 35px for elegance |
| Rounded caps | Disabled | Sharp, minimal endings |
| Value range | 0-100 | Percentage-like for intuitive fill |

**Fault tolerance:** The track uses #1A1A1A (a barely-visible dark gray) rather than pure black. If the indicator arc fails to render, the track remains visible and the user sees a static ring instead of silent nothing. This addresses the reliability-first design principle.

**Arc angle configuration:**
```c
// Full circle for Nothing — indicator fills clockwise from top (0° = 12 o'clock)
lv_arc_set_bg_angles(gauge, 0, 360);   // was 118, 242 or 298, 62
lv_arc_set_range(gauge, 0, 100);        // normalized range
lv_arc_set_value(gauge, current_val);  // fills clockwise from top

// Track: #1A1A1A visible fallback (fault tolerance)
lv_obj_set_style_arc_color(gauge, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);

// Indicator: Nothing red
lv_obj_set_style_arc_color(gauge, lv_color_hex(0xD71921), LV_PART_INDICATOR | LV_STATE_DEFAULT);
```

### 4.3 Text Labels

**Ring identity labels (P1 — fault tolerance):** Each ring has a small identity label to distinguish temperature from pressure. Labels use NothingMuted color at 11px, positioned outside the arc radius to avoid interfering with value display.

- Temperature ring: `°C` label, positioned above the temperature value
- Pressure ring: `bar` label, positioned below the pressure value

**Temperature value:**
- Font: Doto Bold, 24px
- Color: NothingContent (#e8e8e8)
- Position: Center of ring, upper portion

**Temperature unit:**
- Font: Doto Regular, 12px (Doto has no Mono variant; use regular with letter-spacing)
- Color: NothingMuted (#888888)
- Position: Below temperature value

**Pressure value:**
- Font: Doto Bold, 24px
- Color: NothingContent (#e8e8e8)
- Position: Center of ring, lower portion

**Pressure unit:**
- Font: Doto Regular, 12px
- Color: NothingMuted (#888888)
- Position: Below pressure value

**Target value:**
- Font: Doto Regular, 11px
- Color: NothingMuted (#888888)
- Position: Near the target indicator

### 4.4 Target Needle

**Current:** Target shown as rotated `lv_img` needle pointing to target value.

**Nothing variant:** Target needle hidden. The ring fill and target value label communicate the goal without a separate needle. This aligns with the Nothing minimalist aesthetic.

### 4.5 Fault Tolerance Summary

| Failure Mode | Mitigation |
|--------------|------------|
| Doto font load fails | Falls back to LV_FONT_DEFAULT |
| Arc indicator fails to draw | Visible #1A1A1A track remains visible |
| SPIFFS corruption | Font init logs error; graceful degradation |

---

## 5. Component File Structure

```
src/display/ui/default/lvgl/
├── components/
│   ├── ui_comp_dials.c        // Existing — unmodified for other themes
│   └── ui_comp_nothing_dials.c // NEW — Nothing-specific ring gauge
├── fonts/
│   └── ui_nothing_font.c      // NEW — Doto font loading
├── ui_themes.cpp              // MOD — add Nothing color arrays
└── ui_themes.h                // MOD — add UI_THEME_NOTHING enum
```

---

## 6. Theme Switching Logic

When `ui_theme_set(UI_THEME_NOTHING)` is called:
1. `ui_theme_idx` is set to 2
2. All themeable style properties pick up their `[2]` value from color arrays
3. `ui_comp_nothing_dials.c` is used instead of `ui_comp_dials.c` for ring creation
4. Doto font is loaded and available via `ui_nothing_font_get()`

---

## 7. Implementation Scope — v1

**In scope:**
- [ ] Add `UI_THEME_NOTHING` enum (value 2)
- [ ] Add Nothing color arrays to `ui_themes.cpp`
- [ ] Create `ui_nothing_font.c` for Doto TTF loading
- [ ] Copy Doto TTF files to SPIFFS data folder
- [ ] Create `ui_comp_nothing_dials.c` with full-ring arc
- [ ] Verify arc fills correctly from 0-100%

**Out of scope for v1:**
- Other nd-* components (nd-card, nd-stat, nd-stepper, etc.)
- nothing-light variant (white base)
- Custom Nothing Dot font for logo

---

## 8. Success Criteria

1. Ring gauge displays Nothing red (#d71921) fill that grows clockwise from 0° to 360° as value increases
2. Track portion of ring is barely-visible #1A1A1A (visible fallback if indicator fails)
3. Doto font renders temperature/pressure values correctly
4. Theme switch to Nothing shows the ring gauge correctly
5. No flash or artifacts when switching to Nothing theme
6. If Doto font is unavailable, lv_font_montserrat_14 is used as fallback with no crash

**Implementation note:** Add a compile-time assertion to ensure `UI_THEME_NOTHING == 2`:
```c
_Static_assert(UI_THEME_NOTHING == 2, "UI_THEME_NOTHING must be index 2 in theme arrays");
```