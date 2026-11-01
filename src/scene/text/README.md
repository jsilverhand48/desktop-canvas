# src/scene/text/

Specification of Wallpaper Engine text layers: fonts, measuring, line
breaking, rasterizing and drawing (plan phase P6b). Evidence tags as in
`../README.md`. Corpus numbers come from
`claude_tests/corpus/text_survey.py` over the workshop scenes of
2026-10-31 (216 text objects in 46 scenes), and the measuring rules from
fitting this project's measurements to the box `size` the editor stores
with every text object (212 objects whose font file exists).

Text values given by scripts (145 of 216 [corpus]) change at run time
through SceneScript (phase P6a); this phase draws whatever value the
layer holds: the scene file's, a user property's (11 bound), later a
script's.

## 1. Text objects

Fields [corpus; doc: SceneScript ITextLayer]: `text` ("The text that
will be displayed"), `font` ("File path of the active font"),
`pointsize` ("Size of the font in points": 32 in 83, 9 in 80 objects),
`color`, `alpha`, `brightness`, `padding` ("Padding in pixels. This
increases the geometry around the font characters": 32 in 174, the "x y"
string form in 37 [req A1]), `horizontalalign` (center 164, left 44,
right 8), `verticalalign` (center 205, top 11; bottom [doc]), `anchor`
("Dynamic screen anchor": none 172, center 4, left 3, right 2),
`limitrows` with `maxrows`, `limitwidth` with `maxwidth` (pixels),
`limituseellipsis` (16 true), `opaquebackground` with `backgroundcolor`
and `backgroundbrightness` (no object sets it true), `spacing` (always
"0 0"), `blockalign` (always false), `size` (the editor's measured box,
section 3), `effects` (90 objects), the common object fields
(`../model/README.md` section 5).

## 2. Fonts

`font` is looked up like any file (`../assets/README.md` section 2):
the item folder or scene.pkg (180 objects), else the assets folder
(`fonts/<name>`, Wallpaper Engine's own fonts: 32 objects). A name
`systemfont_<family>` (4 objects, all `arial`) is a system font, found
through fontconfig by family [inference: the prefix]. TrueType and
OpenType files load with FreeType [corpus: .ttf 129, .otf 83]. A missing
or unreadable font falls back to the assets' `fonts/NotoSans-Regular.ttf`
and is reported [inference].

## 3. Measuring

- Pixel size: `pointsize` * 300 / 72: Wallpaper Engine lays text out at
  300 DPI [req A7; corpus: with this size every stored height below
  matches to the pixel].
- Line height: the font's line spacing at that size (FreeType's size
  metrics height) [corpus: 114 stored heights equal rows * line height,
  88 more equal it plus twice the padding: 202 of 212].
- Line width: the sum of each character's advance (the unhinted advance
  rounded down to whole pixels), without kerning [corpus: 55 of the 60
  plain texts' stored widths match exactly; HarfBuzz shaping, kerning
  and hinted advances match far fewer]. Text is laid out character by
  character: no shaping of complex scripts (all corpus texts are ASCII).
- The box: the widest line by rows * line height, plus `padding` (x, y;
  a single number for both) on every side ("increases the geometry
  around the font characters" [doc]; the stored sizes of 88 objects,
  among them [req A7]'s "12:34" at 32 pt with padding 32 saved as a 387
  by 219 box, include it). The editor's stored `size` is not used: the
  box follows the current text.

## 4. Lines

Text breaks at newlines. With `limitwidth`, a line wider than `maxwidth`
pixels wraps at its last space that fits, or between characters when a
word alone is wider [doc: "Max width in pixels"; inference: word wrap].
With `limitrows`, only the first `maxrows` rows remain [doc: "Max number
of rows"]; with `limituseellipsis` as well, the last kept row ends in
"..." in place of the characters that no longer fit it ("cut off text
that is too long and replace the end with three dots" [doc: Media
Information]); an ellipsis also ends a row cut by the width limit when
rows are limited to that row [inference].

## 5. Placement

The box is the layer's rectangle (its size, in scene units, before the
layer's scale), placed by the alignments [inference: a left aligned
clock keeps its left edge when its text grows]:
- `horizontalalign` aligns each line in the box (left, center, right)
  and puts the origin at the box's left edge, centre or right edge;
- `verticalalign` puts the origin at the box's top edge (top), centre
  (center) or bottom edge (bottom).
Origin, scale and angles include all parents and Y is up, as for images
[req A3, A7]. `anchor` (dynamic screen anchors), `spacing` and
`blockalign` are not done [corpus: anchors other than none on 9
objects, spacing always 0, blockalign always false].

## 6. Drawing

The text is rasterized on the CPU into a one channel coverage image of
the box, glyph by glyph (8 bit anti-aliased FreeType bitmaps, baseline
at the line's top plus the font's ascender). Sharpness follows the
layer's scale [req A7]: the raster uses `pointsize` * 300/72 *
mean(|scale x|, |scale y|) of the layer's world transform pixels per em,
times the frame's pixels per scene unit when that is above 1 (the
largest frame drawn so far, so outputs of different sizes do not
rasterize in turn) [inference], rounded and clamped to 1..1024; the box
keeps its scene size whatever the raster size.

The layer then draws like an image layer whose texture is that image
(`../render/README.md` sections 2 and 3): Wallpaper Engine's `font`
shader through `materials/fonts/basefont.json` [asset], its basic path
reading the coverage (`ConvertSampleR8`) and painting `g_Color4` = colour
* brightness with alpha [asset: font.frag]. With effects, the font
material first draws the text into the layer's buffer, effects follow,
and the result reaches the frame through translucent pass through
[inference: effects need colour, not coverage]. With
`opaquebackground`, `materials/fonts/fontbackground.json` (the `flat`
shader) first fills the box with `backgroundcolor` *
`backgroundbrightness` [asset; doc: "Adds an opaque background"].
The font shader's MSDF, outline, blur, drop shadow and colour font paths
(`COLORFONT`, emoji) are not used: no corpus text asks for them.

A text layer measures and rasterizes again whenever its text, font,
point size, padding, limits or the raster size change (a script setting
a clock, a user property, a scale animation crossing a whole pixel per
em).

## 7. Capability

`text` (`../model/README.md` section 9) is supported from this phase;
scenes whose texts come from scripts still need `script` (phase P6a).

## Files

(added with the implementation)
