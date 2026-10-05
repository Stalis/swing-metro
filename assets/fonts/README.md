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
The source is rasterized at its native 8 px pixel grid. The 4 and 5 px font roles use its
5 px capital height; the 6 and 7 px roles stretch only vertically. Horizontal strokes are
never resampled, and each glyph has a one-pixel gap for legibility on the 160×128 panel.
Regeneration requires Pillow.
