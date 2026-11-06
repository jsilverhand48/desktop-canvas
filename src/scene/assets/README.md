# src/scene/assets/

Specification and implementation of how a scene's files are found and how
its textures are read. Evidence tags as in `src/scene/README.md`. Corpus
numbers come from the user's library (203 scenes, 1164 package textures)
plus the Wallpaper Engine assets folder (311 textures), measured
2026-10-30.

## 1. scene.pkg archives

Little endian throughout. [corpus 203/203]

| Field | Type |
|---|---|
| version length N | u32 |
| version | N ASCII bytes, `PKGV` + four digits (0001 to 0024 seen; one layout for all) |
| entry count | u32 (at most 232 seen) |
| per entry: name length L | u32 |
| per entry: name | L bytes UTF-8, `/` separated, relative |
| per entry: offset | u32, from the start of the data area |
| per entry: size | u32 |
| data area | starts right after the table, ends at end of file |

Observed in every package: the table is in offset order, the first entry
starts at offset 0, entries tile the data area without gaps or overlaps,
names are unique and contain no backslash, no leading `/` and no `..`.
Names include non ASCII characters (full width brackets, dashes).

Reader rules (this project): the version must be `PKGV` plus digits; the
entry count is capped at 65536 and names at 4096 bytes; every entry must
lie inside the file; names are rejected (the whole archive fails) when they
are empty, absolute, contain `\`, or have a `.` or `..` path segment.
Entries are served as views into a read only memory map of the file.

## 2. File lookup

A scene's paths (`scene.json`, `materials/x.json`, `shaders/y.frag`, ...)
resolve through layers, first match wins:

1. Wallpaper Engine compatibility overrides for shared workshop effects
   (section 2.1);
2. the item's own files: loose files in its folder, then its `scene.pkg`;
3. the Wallpaper Engine assets folder.

Facts behind the order:
- Item content must beat the assets folder: 476 package entries reuse an
  assets path, all under `effects/` (items ship their own copies of
  effects). [corpus]
- All 203 scenes are packaged; the loose files next to a package are
  `.dxs` (1138, compiled Direct3D shader caches starting `SHDV0066` then
  `DXBC`, written by Wallpaper Engine on Windows and ignored here), `.jpg`
  (170), `.gif` (32) and `.png` (1) previews. No loose file also exists
  inside its item's package (0 of 6837 entries). [corpus] Whether loose
  files or the package win is therefore not observable; this project tries
  loose files first, so a hand edited file overrides a packaged one, and an
  unpacked item (no `scene.pkg`) works the same way.

Paths are normalized before lookup: `\` becomes `/`, empty and `.`
segments are dropped; `..` segments and absolute paths are refused, so no
lookup leaves its layer's root. Matching is exact (case sensitive), as
package names are.

### 2.1 Compatibility overrides

Wallpapers embed effects published separately on the workshop under
`<kind>/workshop/<effect id>/...` (`effects/`, `materials/`, `shaders/`,
`particles/`, `models/`, `fonts/` all seen [corpus]). A material names its
shader `workshop/<effect id>/effects/<Name>`, read from
`shaders/workshop/<effect id>/effects/<Name>.frag` and `.vert`. Effects that
embed other effects nest the path (`shaders/workshop/2779107487/workshop/
2084198056/...`).

`assets/zcompat/scene/shaders/<effect id>/` holds replacement shader files
for one such effect plus a `config.json`
`{"frag": "<file>", "vert": "<file>", "maximumprojectid": "<integer>"}`.
[asset: 2078835426 pixelate, maximum 9223372036854775807; 2084198056
Simple_Audio_Bars, maximum 2638335396] Evidence for the meaning [corpus]:
8 scenes embed effect 2084198056, 5 directly and 3 nested (other ids,
3021673417 and 3200298808, hold copies by other authors). The 3 direct ones
with ids above 2638335396 ship a revision that differs from the override
only in annotation key order and one compatibility `#define` (8 lines);
2583042997, below it, ships a
revision that differs in 109 lines, all strict GLSL fixes (float literals,
explicit `float()` and `int()` casts, `mix` for `lerp`, `mod2` for `%` on
uints); 2284874919, also below, ships a much older revision with other
uniform names. So the override is the effect's strict GLSL revision and
`maximumprojectid` is the newest wallpaper (by workshop id) still shipping
an older one. The documentation describes the mechanism: fixes cannot be
pushed into published wallpapers, but "a fixed effect may be shipped in
the program and substituted automatically", only for effects previously
submitted to the Workshop and not radically changed [doc: Shaders on
Mobile Devices, Existing Effects].

Implemented as: when the wallpaper's workshop id is numeric and at most
`maximumprojectid` (a missing value means no limit; an unparsable one
disables the override), a request for
`shaders/workshop/<effect id>/<path>` whose last segment is the configured
frag or vert file name is served from the override folder. Nested copies
are not matched (no scene below a maximum has one). Local projects without
a numeric id are never overridden. Whether Wallpaper Engine also replaces a
revision as old as 2284874919's cannot be observed; phase P3's corpus
compile shows whether the packaged revisions need the override.
`assets/zcompat/web/` holds text patches for web wallpapers (`actions` of
file, replace, insert) and is out of scope here.

## 3. .tex textures

Little endian. Tags are 8 ASCII characters followed by one NUL byte.

### 3.1 Header

| Field | Notes |
|---|---|
| `TEXV0005` | every file [corpus 1475/1475] |
| `TEXI0001` | every file |
| format | u32 pixel format code, section 3.3 |
| flags | u32, section 3.2 |
| texture width, height | u32 x2: the allocated size (often a power of two) |
| (volume only) one u32 | present when flag 0x40 is set; 1024 in all 28 volume textures, meaning unknown, skipped |
| image width, height | u32 x2: the content size, at most the texture size |
| colour | u32, reads as packed 0xAARRGGBB with alpha FF in the values seen (0xFF000000 in 682 files); purpose unknown, not needed |

Texture versus image size [corpus]: raw payloads are stored at the texture
size (1056 of 1056 first mips); JPEG and PNG payloads are stored at the
image size while the texture size is the power of two allocation (391
files). Sampling uses the image region: the usable UV fraction is image /
texture per axis [req A5].

### 3.2 Flags

| Bit | Meaning | Evidence |
|---|---|---|
| 0x1 | no interpolation (nearest filtering) | set exactly where the `.tex-json` import settings say `"nointerpolation": true` [asset 1/1, 271 negatives] |
| 0x2 | clamp UVs (else repeat) | set exactly where `"clampuvs": true` [asset 182/182, 90 negatives]; every scene object with `"clampuvs": true` samples such a texture [corpus 38/38] |
| 0x4 | animated sprite sheet: a frames section follows the images | [corpus 60/60, none of 1387 without it; asset 52/52 with `spritesheetsequences`] |
| 0x20 | video: the image payload is an MP4 file | [corpus 4/4] |
| 0x40 | volume texture (3D colour lookup table): extra header and mip fields | [asset 28/28, all in `materials/lut/`] |
| 0x80000 | unknown | assets folder only, never in packages |

### 3.3 Pixel formats

Measured bytes per pixel of raw payloads, cross checked with the `format`
names of `.tex-json` files that sit next to their compiled texture [asset]:

| Code | Format | Evidence |
|---|---|---|
| 0 | RGBA8888 | 4.0 B/px [corpus 1213]; `rgba8888`, `rgb888`, `rgba8888n` compile to 0 [asset 153]; bytes are R, G, B, A in that order: `materials/gradient/gradient_fire.tex` reads dark red to yellow (as B, G, R it would be blue) [asset] |
| 4 | DXT5 (BC3) | 1.0 B/px; `dxt5`, `dxt5n` compile to 4 [asset 8]; alpha blocks show DXT5 endpoint pairs [corpus] |
| 6 | DXT3 (BC2) | 1.0 B/px; opaque blocks have all-FF explicit alpha, the DXT3 signature [corpus 4897 blocks] |
| 7 | DXT1 (BC1) | 0.5 B/px [corpus 4 textures] |
| 8 | RG88 | 2.0 B/px [corpus 549]; `rg88`, `rg88n` compile to 8 [asset 51] |
| 9 | R8 | 1.0 B/px [corpus 667]; `r8` compiles to 9 [asset 60] |

Block formats use 4x4 blocks (8 bytes for DXT1, 16 for DXT3 and DXT5);
mips smaller than a block still store one whole block per started block
(2x2 mips hold 16 bytes) [corpus]. Other codes are unsupported and reported.

### 3.4 Image container

| Field | Notes |
|---|---|
| `TEXB0001` to `TEXB0004` | 45, 82, 806 and 514 files [corpus] |
| image count | u32; 1 in 1445 files, 2 and 4 in one file each |
| codec (TEXB0003+) | i32: -1 raw pixels (or MP4 with flag 0x20), 2 JPEG, 13 PNG; payload signatures match in every file [corpus 961+4 / 107 / 375] |
| one u32 (TEXB0004) | 0 in every file seen, meaning unknown, skipped |
| per image: mip count | u32 |
| per mip: width, height | u32 x2 |
| per mip: depth (volume only) | u32, 32 in every LUT |
| per mip: LZ4 flag, decompressed size (TEXB0002+) | u32 x2; when the flag is non zero the payload is an LZ4 block (no frame header) of that decompressed size [corpus 1541/1541 verified] |
| per mip: payload size, payload | u32, bytes |

TEXB0001 and TEXB0002 have no codec field and hold raw pixels [corpus
45/45, 82/82].

Mip sizes [corpus]: a mip's width and height fields are the size of its own
payload. Raw payloads hold exactly that many pixels of the format, block
formats rounded up to whole blocks (2763 of 2763 raw mips); JPEG and PNG
payloads decode to exactly that size (every encoded mip); a volume PNG is
width x (height x depth). Each mip halves the one before, rounded down, at
least 1 (3193 of 3193). The first mip is at the texture size for raw images
and at the image size for JPEG and PNG, except in the two multi-image
sprite sheets, whose later images are smaller (4096x4096 then 2048x2048;
three of 2048x2048 then 2048x1024: the last image holds the remaining
frames). Per image sizes therefore come from the mips, not the header. The
largest first mip decodes to 137 MiB of RGBA (8001 x 4501); no dimension
exceeds 8192.

After the last image, when flag 0x4 is set:

| Field | Notes |
|---|---|
| `TEXS0002` or `TEXS0003` | 8 and 52 files [corpus] |
| frame count | u32 |
| frame width, height (TEXS0003) | u32 x2, the source animation's frame size |
| per frame (32 bytes) | u32 image index, f32 duration in seconds, f32 x, f32 y, f32 ux, f32 uy, f32 vx, f32 vy |

A frame is the parallelogram at (x, y) spanned by (ux, uy) and (vx, vy) in
the pixels of the image it names (in both multi-image sheets every image's
frames stay inside that image's own size); every one of the 381 frames
seen is axis aligned (uy = vx = 0), so it is the rectangle x, y, ux, vy
[corpus]. Durations seen: 0.1, 0.0625 and 0.03 s. 22 frames point at
images other than the first.

Volume textures (flag 0x40) hold one PNG of 32 x 1024 pixels: 32 slices
of 32 x 32 stacked vertically [asset 28/28]. Their header reads texture and
image size 32 x 32, the extra u32 1024 (32 x 32), flags 0x42, and one mip
of 32 x 32 x depth 32.

Reader rules (this project): files must be consumed exactly. Anything left
over, a size that runs past the end, no images, an image without mips, a
width, height or depth of 0 or above 16384, more than 64 images or 16 mips
per image, a frame count larger than the bytes left, a frame naming a
missing image or holding a non finite value, an unknown codec, or a
TEXV, TEXI, TEXB or TEXS version not listed above is an error. Unknown pixel format codes parse (the report names them) and
fail at decoding.

## 4. Decoding

Decoded results are tightly packed rows, top to bottom:
- RGBA8888 as is; R8 and RG88 as one and two channels (the GL upload keeps
  them as R8 and RG8 textures);
- DXT1/3/5 stay compressed for upload (S3TC is universal on desktop GL),
  and a CPU decoder to RGBA8 exists for screenshots, tests and drivers
  without the extension. It follows the public block definitions (Khronos
  Data Format Specification, S3TC section; Direct3D 11 block compression
  documentation for BC1 to BC3) with Direct3D's rules, the API Wallpaper
  Engine targets (its `.dxs` caches hold DXBC bytecode [corpus]): DXT1
  uses the three colour mode with transparent black when colour 0 <=
  colour 1; the colour blocks of DXT3 and DXT5 always use four colours. An
  opt-in test compares it with the GL driver's own decoding;
- JPEG (libjpeg-turbo) and PNG (libpng) decode to RGBA8 at the mip size;
  such textures always carry format code 0, RGBA8888 [corpus 107/107 and
  403/403], so the result matches the header;
- MP4 payloads are handed to the video texture path (phase P4b).

A payload whose size (raw, after LZ4) or decoded dimensions differ from
its mip fields is an error, and so is any single decoded buffer above
256 MiB (1.9 times the largest seen).

### 4.1 Tools

`desktop-canvas-scene --extract ENTRY OUT WALLPAPER_DIR` looks ENTRY up
through the layers of section 2; a `.tex` file is decoded (first image,
first mip; block formats expanded) and written as a PNG with 1, 2 or 4
channels, its header printed as JSON; any other file is copied as is.
`--dump DIR WALLPAPER_DIR` writes every `scene.pkg` entry below DIR, which
must be empty or absent; existing files are never overwritten.

## Files

All in namespace `canvas::scene`, built into `canvas_scene_core`.

- `ByteReader.h` bounds checked little endian reader over a byte span;
  every read is checked, the first failure sticks and carries the offset.
- `MappedFile` read only memory map of a file (RAII). `openBeneath(root,
  relative)` opens with `openat2(RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS)`
  (canonical path check on kernels before 5.6), so symlinks cannot lead a
  lookup out of its layer's root.
- `PackageArchive` scene.pkg per section 1: validates the whole table in
  `open()`/`parse()`, then serves entries as views into the mapping.
- `AssetLocator` the lookup layers of section 2 for one wallpaper, the
  section 2.1 overrides (read from `config.json` with nlohmann/json), and
  `guessAssetsDir()` for the Steam library holding an item. `find()` keeps
  no state; loose and assets files come back with their own mapping.
- `TextureFile` parsing per section 3 (no pixel work): `parseTexture()`
  returns the header, images with their mips (payload views) and frames.
- `TextureDecode` per section 4: `decodeMip()` (LZ4 through
  `LZ4_decompress_safe`, JPEG through the TurboJPEG 2.x interface of
  libjpeg-turbo 2.1 or later with the progressive scan limit and the size
  checked before allocating, PNG through libpng's simplified API, raw size
  checks) and `expandBlocks()` (CPU DXT1/3/5 to RGBA8);
  `decodeImageFile()` decodes a whole JPEG or PNG file with its size from
  its header (album art for media events, `../audio/README.md` section
  7). Stateless, any thread.

Tests: `claude_tests/test_scene_assets.cpp`, `test_scene_corpus.cpp`,
`test_scene_dxt_gl.cpp` and `fuzz/` (see `claude_tests/README.md`).
