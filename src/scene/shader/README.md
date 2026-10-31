# src/scene/shader/

Specification and implementation of Wallpaper Engine's shader dialect and
its translation to desktop GLSL 3.30 core. Evidence tags as in
`src/scene/README.md`; [doc] pages are "Shader Syntax Overview", "Shader
Variables & Constants", "Shader Headers" and "Shaders on Mobile Devices"
of docs.wallpaperengine.io. Corpus numbers come from
`claude_tests/corpus/shader_survey.py` over the 203 scenes (861 vertex and
fragment pairs in packages, 312 distinct files) and the assets folder (59
pairs, 14 headers), measured 2026-10-30.

## 1. The dialect

Shaders are old style GLSL: `attribute`, `varying`, `uniform` and
`gl_FragColor` [doc: Syntax; corpus: every package shader declares
varyings and every package fragment shader writes gl_FragColor (861);
in the assets folder 10 files declare `in` and 3 `out`]. Wallpaper Engine compiles them as HLSL (Direct3D 11) on
desktop and as GLSL ES 3.00 on mobile, translating a set of cross
platform names [doc: Mobile]:

| Dialect | GLSL (as Wallpaper Engine's GLSL ES path translates) | Files using it [corpus pkg / assets] |
|---|---|---|
| `texSample2D(s, uv)` | `texture(s, uv)` | 838 / 47 |
| `texSample2DLod(s, uv, lod)` | `textureLod(s, uv, lod)` | 43 / 14 |
| `mul(x, y)` | `((y) * (x))` | 670 / 57 |
| `frac` | `fract` | 280 / 8 |
| `atan2` | `atan` | 38 / 1 |
| `lerp` | `mix` (both accepted) | 46 / 2 |
| `saturate(x)` | `clamp(x, 0.0, 1.0)` | 393 / 32 |
| `fmod(x, y)` | `(x-y*trunc(x/y))` | 2 / 0 |
| `ddx` | `dFdx` | 3 / 2 |
| `ddy(x)` | `dFdy(-(x))` | 3 / 2 |
| `CAST2(x)`, `CAST3(x)`, `CAST4(x)`, `CAST3X3(x)` | `vec2(x)`, `vec3(x)`, `vec4(x)`, `mat3(x)` [doc: Syntax] | 108, 111, 219, 4 / 13, 30, 2, 17 |

`mul` argument order [corpus]: 820 calls pass a vector then a matrix
(`mul(vec4(a_Position, 1.0), g_ModelViewProjectionMatrix)`), 2 a matrix
then a vector, 4 two matrices; with the mapping above, matrices are
uploaded in the usual GL column vector convention (phase P4).

The documented `ddy` mapping negates its argument, which fits a GL render
that is upside down relative to Direct3D; phase P4 renders with that
orientation and its comparisons confirm the reading (this project).

On GLSL ES, Wallpaper Engine "appends .0 to integer constants it finds"
unless the file defines `WEMOBILE_DISABLE_INTEGER_CONVERSION` [doc:
Mobile]. Desktop GLSL 3.30 has implicit integer to float conversions of
its own, so this project starts without rewriting literals; section 7
records what the drivers accept and which rewrites the failures need.

Vertex attributes [doc: Syntax]: `a_Position` vec3, `a_PositionVec4`,
`a_Normal`, `a_Tangent4`, `a_TexCoord` vec2, `a_TexCoordVec3`,
`a_TexCoordVec4`, `a_TexCoordC1` to `C5` (with Vec3 and Vec4 forms),
`a_Color`; image and text layers provide only `a_Position` and
`a_TexCoord`.

## 2. Sources and includes

A material pass names its shader `<name>`; the program is
`shaders/<name>.vert` plus `shaders/<name>.frag`, found through the lookup
of `../assets/README.md` section 2 (so workshop effect overrides apply).
`#include "file"` names `shaders/<file>` through the same lookup: 912
includes in packages, all of Wallpaper Engine's own headers (`common.h`
505, `common_blending.h` 285, `common_blur.h` 53, `common_perspective.h`
47, `common_composite.h` 10, `common_fragment.h` 8, `common_vertex.h` 3,
`common_pbr.h` 1) [corpus], which ship in the assets folder [asset] and
are used as they are. Headers carry no include guards yet some include
others (`common_composite.h` includes `common.h` and
`common_blending.h`) [asset], so each file is included at most once per
shader (later includes of it expand to nothing). Include depth is limited
to 16 and an include that is not found is an error for that program.

Line ends are CRLF in every file seen [corpus 1858/1858]; lone CR (seen
in other workshop items [req A2]) and CRLF are read as line breaks.

## 3. Annotations

Lines starting `// [COMBO]` followed by JSON declare a combo [doc:
Variables]: `combo` (the define name), `default` (number), `material`
(editor label), `type` (`options` 1343, `imageblending` 294,
`audioprocessingoptions` 123, absent 61 [corpus]), `options` (label to
value) and `require` (combos that must have given values for the editor
to show it; ignored here). `// [COMBO_OFF]` (28), `// [OFF_COMBO]` (17)
and `// [COMBO_DISABLED]` (1) carry the same JSON but declare nothing:
authors switched them off. `// [PASS]` (assets, 3) is not JSON and
ignored.

A uniform declaration may end with a JSON comment [doc: Variables]:
`uniform float g_Strength; // {"material":"strength","default":1}`.
Keys seen on scalars [corpus]: `material` 5503, `default` 5492, `label`
4247, `range` 4170, `group` 762, `type` ("color") 394, `linked` 361,
`int` 258, `direction` 160, `position` 66, `nobindings` 26, `conversion`
("rad2deg" 20, "startdelta" 1), `require` 20, `order` 6, `hidden` 1; on
samplers: `label` 1363, `material` 1091, `hidden` 1032, `default` 814
(a texture or render target), `mode` 812 (opacitymask 677, flowmask 94,
rgbmask 32, depth 6, normal 3), `combo` 564, `paintdefaultcolor` 463,
`require` 58, `formatcombo` 22, `format` 18 (normalmap, r8, rg88),
`nonremovable` 10, `components` 8, `requireany` 8. 2 annotations are not
valid JSON and are ignored.

The `material` name links a uniform to the `constantshadervalues` of
materials and effect instances (`../model/README.md` sections 5.1 and 7);
`default` is used when neither sets it. `conversion` "rad2deg" means the
editor shows degrees for a value stored in radians: the stored value is
used as is.

Samplers `g_Texture0` to `g_Texture7` (and beyond in assets) are bound by
index; their resolution uniforms are engine values (section 5).

## 4. Combos and the preprocessor

Combos become integer defines. A program's combo values, later wins:
the `[COMBO]` defaults; the material pass `combos`; the effect instance
pass or image instance `combos` from scene.json; a sampler's `combo`
define is 1 when its texture slot is set by the material or the instance
and 0 otherwise [doc: Variables: the combo "is set to 1 when the user
paints or selects a texture, and to 0 when no texture is bound"]; a
sampler with `formatcombo` defines `TEX<n>FORMAT` as its texture's format
code, the codes of `../assets/README.md` section 3.3, which the assets
headers name `FORMAT_RGBA8888` 0, `FORMAT_DXT5` 4, `FORMAT_DXT3` 6,
`FORMAT_DXT1` 7, `FORMAT_RG88` 8, `FORMAT_R8` 9 [asset: common_fragment.h;
5 package shaders use TEX2FORMAT]. `GLSL` is defined as 1 [doc: Syntax
context defines]; `HLSL`, `HLSL_SM30`, `HLSL_SM40`, `HLSL_GS40` are not.

Conditionals are resolved here, not by the driver, so every driver sees
the same code and the program's real interface is known: `#if`,
`#ifdef`, `#ifndef`, `#elif` (used 63 times although the documentation
says it is unsupported [doc: Syntax; corpus]), `#else`, `#endif`, with
C rules: integer arithmetic, comparison, `&&`, `||`, `!`, `defined()`,
and identifiers that are not defined evaluate to 0 (combos switched off
by default are often never defined: `#if MASK`). `#if` forms seen
[corpus, 8232 in packages and assets]: comparisons with numbers (4671), bare names (2529),
comparisons of two names (478), `>=`, `!=`, `||`, `&&`, `!` and
parentheses. Object like `#define NAME value` lines are tracked for the
expressions and passed through for the driver; `#undef` likewise.
`#require LightingV1` (assets model shaders, 7) is dropped. A stray
`#endif` (3 package files) becomes a comment line without the original
text, and blocks still open at the end are closed [req A2 (4)].

## 5. Engine uniforms

Uniforms declared without an annotation are provided by the engine
(phase P4 sets them) [doc: Variables; corpus, files declaring each]:
`g_ModelViewProjectionMatrix` 733, `g_Time` 616, `g_Texture<n>Resolution`
(vec4: physical size xy, mapped size zw) 568 / 498 / 231 / 80 for n 0 to
3, `g_AudioSpectrum16Left/Right` 158 / 154 and the 32 and 64 versions
31 to 32, `g_PointerPosition` 30, `g_EffectModelViewProjectionMatrix` 20,
`g_ViewProjectionMatrix` 18, `g_ModelMatrix` 17,
`g_ModelViewProjectionMatrixInverse` 16, `g_EyePosition` 13, `g_Screen`
11, `g_TexelSize` 8, `g_Texture0Rotation` and `Translation` (sprite
sheets) 8 / 6, `g_ParallaxPosition` 5, `g_Color4` 5, `g_Alpha` 3, plus
lighting, fog, bone and morph uniforms used only by the assets' 3D model
shaders. Uniforms without annotation that the engine does not know stay
at zero and are reported.

## 6. Translation to GLSL 3.30 core

The program text handed to the driver: `#version 330 core`, the defines
of section 4, the prelude (the mappings of section 1 as macros), then the
resolved source with these rewrites, on whole identifiers outside
comments:
- vertex: `attribute` becomes `in`, `varying` becomes `out`;
- fragment: `varying` becomes `in`, and `gl_FragColor` becomes a declared
  `out vec4` (a macro may not take a `gl_` name);
- the compatibility fixes of [req A2], in this order, each logged: (1) a
  fragment shader using a varying that only its vertex shader declares
  gets the declaration after the last `#extension` line or at the top;
  (2) a uniform float array indexed `[a][b]` as packed float4 groups is
  indexed `[4*a + b]`, and `[i / 4][i % 4]` becomes `[i]`; (3) assignments
  to varyings in a fragment shader (`=`, `+=`, `-=`, `*=`, `/=`, through a
  swizzle too, never `==`) go to a global copy initialised at the start of
  `main`, and every use reads the copy.

`glslang` checks the result against the GLSL 3.30 core rules as a strict
reference (tests and the corpus experiment); drivers differ in leniency
(NVIDIA accepts what Mesa rejects).

## 7. Corpus experiment

Every program the library uses (object materials and effect passes, with
their resolved combos) is translated and compiled on the three drivers
of this machine (NVIDIA, radeonsi, llvmpipe) through headless EGL
contexts, and checked with glslang. Results, failure classes and the
fixes they led to are recorded here before rendering builds on them.

(results pending)

## 8. Program cache

Linked programs are kept in memory per (vertex source, fragment source)
and on disk under `$XDG_CACHE_HOME/desktop-canvas/shaders/` as program
binaries. A file is named by a 64 bit hash of the driver string and both
sources but stores them in full, and a binary is used only when they
match exactly, so hash collisions and driver updates fall back to
compiling. Compiles use `GL_KHR_parallel_shader_compile` when present.

## Files

(added with the implementation)
