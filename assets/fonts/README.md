# Silkscreen Flat

`SilkscreenFlat-Regular.ttf` is a modified copy of [Silkscreen Regular](https://github.com/googlefonts/silkscreen/blob/main/fonts/ttf/Silkscreen-Regular.ttf). It retains the original glyphs and adds a distinct lowercase `b` at U+0062 for flat note names such as `Bb0` and `Eb0`.

The `b` follows Silkscreen's 125-unit pixel grid and uses this 3×5 pattern:

```text
#..
#..
###
#.#
###
```

The family is named **Silkscreen Flat** so it can coexist with the original font in Figma. The typeface is distributed under the [SIL Open Font License 1.1](OFL.txt); the original copyright and license metadata remain in the font.

`tools/generate_silkscreen_fonts.py` produces the firmware's 1-bit LVGL fonts from this TTF.
The 4–7 px display sizes use compact pixel grids so their glyphs have hard edges and remain
readable on the 160×128 RGB565 panel. Regeneration requires Pillow.
