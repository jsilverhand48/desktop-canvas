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

1. Wallpaper Engine compatibility overrides for this workshop item
   (section 2.1);
2. the item's own files: its `scene.pkg` and loose files in its folder;
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

`assets/zcompat/scene/shaders/<workshop id>/` holds replacement shader
files for single workshop items plus a `config.json`
`{"frag": "<file>", "vert": "<file>", "maximumprojectid": "<integer>"}`.
[asset: entries for 2078835426 and 2084198056] Neither item is in the
corpus, so the effect cannot be observed. Implemented as: for the item with
that id, a request for `shaders/<file>` where `<file>` is the configured
frag or vert name is served from the override folder. `maximumprojectid`
(9223372036854775807 and 2638335396 seen) has no observable meaning yet and
is ignored. `assets/zcompat/web/` holds text patches for web wallpapers
(`actions` of file, replace, insert) and is out of scope here.

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
| 0 | RGBA8888 | 4.0 B/px [corpus 1213]; `rgba8888`, `rgb888`, `rgba8888n` compile to 0 [asset 153] |
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

After the last image, when flag 0x4 is set:

| Field | Notes |
|---|---|
| `TEXS0002` or `TEXS0003` | 8 and 52 files [corpus] |
| frame count | u32 |
| frame width, height (TEXS0003) | u32 x2, the source animation's frame size |
| per frame (32 bytes) | u32 image index, f32 duration in seconds, f32 x, f32 y, f32 ux, f32 uy, f32 vx, f32 vy |

A frame is the parallelogram at (x, y) spanned by (ux, uy) and (vx, vy) in
the image's pixels; every one of the 381 frames seen is axis aligned
(uy = vx = 0), so it is the rectangle x, y, ux, vy [corpus]. Durations seen:
0.1, 0.0625 and 0.03 s. 22 frames point at images other than the first.

Volume textures (flag 0x40) hold one PNG of 32 x 1024 pixels: 32 slices
of 32 x 32 stacked vertically [asset 28/28].

Files must be consumed exactly: anything left over, a size that runs past
the end, a decompressed size above 256 MiB, dimensions above 16384, more
than 64 images or 16 mips per image is an error.

## 4. Decoding

Decoded results are tightly packed rows, top to bottom:
- RGBA8888 as is; R8 and RG88 as one and two channels (the GL upload keeps
  them as R8 and RG8 textures);
- DXT1/3/5 stay compressed for upload (S3TC is universal on desktop GL),
  and a CPU decoder to RGBA8 exists for screenshots, tests and drivers
  without the extension;
- JPEG (libjpeg-turbo) and PNG (libpng) decode to RGBA8 at the image size;
- MP4 payloads are handed to the video texture path (phase P4b).

## Files

- `ByteReader.h` bounds checked little endian reader over a byte span;
  every read is checked, failures carry the offset.
- `MappedFile` read only memory map of a file (RAII).
- `PackageArchive` scene.pkg per section 1.
- `AssetLocator` the lookup layers of section 2 for one wallpaper.
- `TextureFile` parsing per section 3 (no pixel work).
- `TextureDecode` LZ4, DXT and image codec decoding per section 4.
