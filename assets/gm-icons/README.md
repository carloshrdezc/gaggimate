# GaggiMate "Nothing" Icon Set

Ten monochrome line icons (24-unit grid, 1.7px stroke, round caps) for the round
480×480 display theme. Single-color and recolorable at runtime — drawn to sit
beside the Ndot numerals without competing with them.

| Icon | Codepoint | Symbol macro | Used for |
|------|-----------|--------------|----------|
| power  | U+E001 | `GM_SYM_POWER`  | Standby |
| cup    | U+E002 | `GM_SYM_CUP`    | Brew |
| steam  | U+E003 | `GM_SYM_STEAM`  | Steam |
| drop   | U+E004 | `GM_SYM_DROP`   | Water |
| wifi   | U+E005 | `GM_SYM_WIFI`   | Status |
| bt     | U+E006 | `GM_SYM_BT`     | Status |
| sun    | U+E007 | `GM_SYM_SUN`    | Settings · brightness |
| thermo | U+E008 | `GM_SYM_THERMO` | Settings · temperature |
| scale  | U+E009 | `GM_SYM_SCALE`  | Settings · scale |
| back   | U+E00A | `GM_SYM_BACK`   | Close |

## Files

| Path | Purpose |
|------|---------|
| `svg/*.svg` | Icon masters (vector — use for the symbol-font route) |
| `png/*.png` | 96px white-on-transparent masters (use for the A8-image route) |
| `gen_a8_images.py` | Generates the LVGL A8 image sources from the PNGs |
| `../../src/display/ui/default/lvgl/gm_icons.h` | Codepoints, `GM_SYM_*` strings, image/font declarations |
| `../../src/display/ui/default/lvgl/images/gm_ic_*.c` | Generated LVGL v8 `ALPHA_8BIT` image sources |

## How the icons are wired today (Route B · A8 images)

This is the **active** route — it needs no font toolchain and is what the screens
use now. Each `gm_ic_<name>` is an `LV_IMG_CF_ALPHA_8BIT` image: an alpha mask that
LVGL paints in the widget's `(bg_)img_recolor` color. Tint it at runtime:

```c
lv_img_set_src(icon, &gm_ic_cup);
lv_obj_set_style_img_recolor(icon, lv_color_hex(0xE8E8E8), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_img_recolor_opa(icon, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
```

> ⚠️ `ALPHA_8BIT` images render **entirely** in the recolor color (default black).
> Always set `img_recolor` (or `bg_img_recolor`), or the icon is black-on-black.
> The existing screen widgets already set this to the theme `NiceWhite`.

### Regenerate the image sources

```sh
python assets/gm-icons/gen_a8_images.py --size 48   # → src/.../lvgl/images/gm_ic_*.c
```

Change `--size` to re-cut at a different resolution (e.g. `--size 96` for the full
master, `--size 28` for a status-bar cut). Requires Pillow (`pip install pillow`).

## What's still missing (Route A · symbol font)

The design's *recommended* route renders the icons as a font glyph so they recolor
via plain `text_color`. It can't be produced from this repo alone — it needs an
external font toolchain. To enable it:

1. **Build the TTF.** Import `svg/*.svg` into [IcoMoon](https://icomoon.io), assign
   each glyph its codepoint from the table above (U+E001–U+E00A), export `gm_icons.ttf`.
2. **Convert to LVGL fonts** with [`lv_font_conv`](https://github.com/lvgl/lv_font_conv):
   ```sh
   lv_font_conv --font gm_icons.ttf --size 28 --bpp 4 --format lvgl \
     --range 0xE001-0xE00A -o gm_icons_28.c --force-fast-kern-format
   lv_font_conv --font gm_icons.ttf --size 16 --bpp 4 --format lvgl \
     --range 0xE001-0xE00A -o gm_icons_16.c --force-fast-kern-format
   ```
3. Drop `gm_icons_28.c` / `gm_icons_16.c` into `src/display/ui/default/lvgl/fonts/`,
   add them to `filelist.txt` + `CMakeLists.txt`, and set `#define GM_USE_ICON_FONT 1`
   (in `gm_icons.h` or your build flags). Then draw icons as labels:
   ```c
   lv_obj_set_style_text_font(lbl, &gm_icons_28, 0);
   lv_label_set_text(lbl, GM_SYM_CUP);
   lv_obj_set_style_text_color(lbl, GM_BG, 0);  // knockout on an accent fill
   ```

## Scope note

`sun`, `scale`, and `back` ship as assets but aren't wired into a screen yet —
they're for the quick-settings / close affordances of the larger "Nothing" theme
(`gm_ui.cpp`, `gm_theme.h` in the design bundle), which is a separate effort. The
other seven (power, cup, steam, drop, wifi, bt, thermo) replace the legacy
SquareLine icons in the Menu / Standby / Brew / Status / SimpleProcess / Profile
screens.
