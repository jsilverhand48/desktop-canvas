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
| `ddy(x)` | `dFdy(-(x))`; desktop-canvas uses `dFdy(x)` (below) | 3 / 2 |
| `CAST2(x)`, `CAST3(x)`, `CAST4(x)`, `CAST3X3(x)` | `vec2(x)`, `vec3(x)`, `vec4(x)`, `mat3(x)` [doc: Syntax] | 108, 111, 219, 4 / 13, 30, 2, 17 |
| `log10(x)` | `(log2(x) * 0.30102999566398120)`: an HLSL intrinsic GLSL lacks, not documented [HLSL spec] | 1 / 0 |

`mul` argument order [corpus]: 820 calls pass a vector then a matrix
(`mul(vec4(a_Position, 1.0), g_ModelViewProjectionMatrix)`), 2 a matrix
then a vector, 4 two matrices; with the mapping above, matrices are
uploaded in the usual GL column vector convention (phase P4).

The documented `ddy` mapping negates its argument because Direct3D's
screen Y runs down where GL's runs up, which fits a frame drawn bottom
row first. desktop-canvas lays every buffer out top row first, as
Direct3D does (`../render/README.md` section 1), so GL's `dFdy` already
runs the way `ddy` does and the prelude maps `ddy` to plain `dFdy`.

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
code (particle materials also get `TEX0FORMAT` from their albedo,
`../particles/README.md` section 6), the codes of `../assets/README.md` section 3.3, which the assets
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
- vertex: `attribute` becomes `in`, `varying` becomes `out`; the
  documented attributes get fixed locations (`a_Position` 0, `a_TexCoord`
  1, `a_Normal` 2, `a_Tangent4` 3, `a_Color` 4, `a_PositionVec4` 5,
  `a_TexCoordVec3` 6, `a_TexCoordVec4` 7, `a_TexCoordC1` to `C5` 8 to 12,
  their Vec3 and Vec4 forms written either way round: `a_TexCoordC1Vec4`
  or `a_TexCoordVec4C1` as genericparticle.vert has it [asset]), so every
  program takes the same vertex layout;
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

- uniform declarations of built in types move above the code (array
  sizes given by macros are expanded): Wallpaper Engine's own effect
  shaders include headers that use uniforms the shader declares after the
  `#include` (section 7);
- `#require LightingV1` asks for engine provided lighting code. A program
  that calls `PerformLighting_V1` gets desktop-canvas's own function
  (phase P8, `../render/README.md` section 12) inserted before the first
  function definition, together with the light uniform arrays it reads
  (declared only when the shader does not already declare them) and the
  `LIGHTS_*` combos (zero lights unless the renderer sets counts); one
  that does not call it is unaffected.

`glslang` checks the result against the GLSL 3.30 core rules as a strict
reference (tests and the corpus experiment); NVIDIA's core profile turned
out to be as strict (section 7).

### 6.1 HLSL conversions

Wallpaper Engine compiles these shaders as HLSL on desktop, so authors
rely on HLSL's implicit conversions, which GLSL lacks (section 7). The
rules, from the HLSL language specification [HLSL spec:
microsoft/hlsl-specs, conversions]: a scalar splats into every element of
a vector or matrix; a vector truncates to a smaller vector (elements
[0..n)) or to its first element, a matrix per dimension; every numeric
type converts to every other (float to integer truncates), and to bool
by being non zero; a conversion sequence is one numeric conversion then
one splat or truncation.

After the rewrites of section 6, each stage is macro expanded by this
project's own expander (object and function like macros, C rescanning:
a replacement is rescanned with the tokens that follow it, which
`common_blending.h` needs for `#define BlendDarken BlendDarkenf` [asset];
`#` and `##` do not exist in the dialect [doc: Syntax]), parsed and type
checked, and a
GLSL constructor `T(e)` is inserted wherever a value of type U is used
as a different type T and GLSL would not convert it by itself (int and
uint to float it does):
- initializers, assignments (also through swizzles) and the right side
  of compound assignments: to the target's type; return values: to the
  function's return type;
- arguments: to the parameter types of the chosen overload. Built in
  functions resolve the HLSL way when no GLSL overload fits as written:
  the vector size is the smallest among vector arguments (scalar if none),
  the base type float when any argument is float, generic parameters take
  that type and scalar parameters stay scalar (`max(0, v.rgb)` becomes
  `max(vec3(0), v.rgb)`, `pow(vec3, float)` splats the exponent,
  `mix(vec4, vec3, float)` truncates the first argument, `texture(s,
  vec4)` truncates the coordinate to the sampler's dimension). User
  functions: the declared overload needing the fewest conversions, never
  through `out` or `inout` parameters;
- operands: vectors of different sizes truncate to the smaller; integer
  and unsigned operands meet as unsigned, booleans as the other operand's
  type; `%` with a float operand becomes `x - y * trunc(x / y)` (the
  `fmod` meaning); comparisons and `?:` branches meet at a common type;
- conditions (`if`, loops, `?:`, `!`, `&&`, `||`): to bool; array
  indices: to int;
- a fragment input that the vertex stage outputs wider (HLSL matches
  stages by semantic) takes the vertex type, and its uses convert; a
  vertex output that the fragment stage reads wider takes the fragment
  type and is padded with zeros where it is assigned;
- a local `const` whose initializer is not a constant expression loses
  `const` (HLSL's const only forbids writes; GLSL wants a constant).

Code the parser does not understand, and expressions whose types cannot
be known (an identifier or function it has not seen), are left as they
are. A stage that does not parse keeps the text of section 6, with a
note, so this pass never makes a program worse than without it.

## 7. Corpus experiment

Every program the library uses (object materials and effect passes, with
their resolved combos) is translated and compiled on the three drivers
of this machine (NVIDIA, radeonsi, llvmpipe) through headless EGL
contexts, and checked with glslang (`claude_tests/test_shader_corpus`).
Results, failure classes and the fixes they led to are recorded here
before rendering builds on them.

Run of 2026-10-30: 2431 passes in 203 scenes, 355 distinct programs.

| | NVIDIA 595 | radeonsi | llvmpipe | glslang |
|---|---|---|---|---|
| programs compiling and linking | 319 | 318 | 318 | 319 |
| scenes with every program working | 156 | 155 | 155 | 155 |

The drivers disagree on 1 program; NVIDIA's core profile is as strict as
Mesa here (`error C7011: implicit cast`). 318 programs (90 %) work on
every driver, and 155 scenes (76 %) have every program working on every
driver. 2 more scenes call `PerformLighting_V1` (`#require LightingV1`),
engine code to be written with lights (phase P8).

What the first run taught, now part of sections 2 and 6: includes must
be expanded inside the conditional preprocessor (one run failed every
program); and uniforms must be hoisted, since Wallpaper Engine's own
`effects/blur_precise_gaussian`, `shine_gaussian` and `godrays_gaussian`
include `common_blur.h`, which samples `g_Texture0`, before declaring it
(21 programs).

The 37 programs that still failed (46 scenes) relied on HLSL's implicit
conversions, which GLSL does not have: scalar to vector promotion and
vector truncation in built in and user function calls (`max(0,
albedo.rgb)` in Wallpaper Engine's own `effects/nitro`, used by 24
scenes; `pow(vec3, float)`; `mix(vec4, vec3, float)`; `texture(s,
vec4)`), `%` on floats (the `Simple_Audio_Bars` copies of effect
3021673417, 20 scenes), truncation or broadcast in assignments and
initializers, arithmetic between vectors of different sizes, float array
indices, and varyings declared with different widths in the two stages.
Fixing them needs the types of expressions, so the decision (2026-10-30,
with the user) was this project's own type checker: section 6.1.

Run of 2026-10-31 with section 6.1 (the conversion pass prints programs
normally, so programs that differed only in comments or macros now
coincide: 270 distinct):

| | NVIDIA | radeonsi | llvmpipe | glslang |
|---|---|---|---|---|
| programs compiling and linking | 269 | 269 | 269 | 270 |
| scenes with every program working | 200 | 200 | 200 | 201 |

The drivers agree on every program. Programs the pass changed, by kind
of conversion (one program can need several): truncation 13, splat 11,
numeric 8, `%` as fmod 6, stage widening 3, index to int 2, condition to
bool 1, const dropped 1. Left: one
oscilloscope effect whose vertex stage passes `vec4 audioValue[60]` to
the fragment stage, more interpolated vectors than GL drivers offer (32;
Direct3D 11 has 32 as well), and the 2 scenes needing `LightingV1`
(phase P8). 200 of 203 scenes therefore have every shader they use
compiling on every driver.

## 8. Program cache

Linked programs are kept in memory per (vertex source, fragment source)
and on disk under `$XDG_CACHE_HOME/desktop-canvas/shaders/` as program
binaries. A file is named by a 64 bit hash of the driver string and both
sources but stores them in full, and a binary is used only when they
match exactly, so hash collisions and driver updates fall back to
compiling. Compiles use `GL_KHR_parallel_shader_compile` when present.
All three drivers here offer both the extension and program binaries
(`claude_tests/test_scene_gl`).

## 9. Tracer bullet

Wallpaper Engine's own `genericimage2`, translated with its default
combos and the engine values set (MVP, `g_Color4`, texture unit and
resolution, annotated defaults), draws a textured quad identically on
NVIDIA, radeonsi and llvmpipe (`claude_tests/test_scene_gl`, 2026-10-30).
Unannotated engine uniforms such as `g_Color4` default to zero in GL, so
the renderer must always set them.

## Files

Namespace `canvas::scene`, library `canvas_scene_core` (no GL).

- `Preprocessor` sections 2 and 4: `ShaderPreprocessor` (includes once
  per run inside active code, C rules for conditionals, macro tracking,
  [req A2 (4)] repairs, `#require` and `#extension` collection) and
  `normalizeLineEnds`.
- `Translator` sections 3 to 6: `translateProgram` (combo resolution,
  preprocessing of both stages, interface parsing, rewrites, uniform
  hoisting, fixed attribute locations, [req A2 (1) to (3)], assembly),
  `shaderPrelude`, `attributeLocation`, `replaceIdentifiers`.
- `Programs` `collectPrograms` (every material and effect pass of a
  loaded scene, and the material of every particle file a particle layer
  draws, children included, as a `ProgramRequest`), `particleRequest` (a
  particle material's combos and, for ropes, genericropeparticle,
  `../particles/README.md` sections 6 and 6.1) and `shaderLoader`
  (sources through an `AssetLocator`).
- `Tokens` section 6.1's tokenizer and macro expander (`expandProgram`).
- `Syntax` GLSL types (`GlslType`, `builtinType`), the syntax tree, the
  recursive descent parser (`parseGlsl`) and printer (`printGlsl`). The
  printer keeps a unary `-` or `+` apart from an operand that starts with
  the same sign: a macro holding `-1.3` after a minus must print as
  `- -1.3`, not the decrement operator (3058007295).
- `Conversions` section 6.1's type checker (`applyHlslConversions`):
  scopes, structs, user overloads, the built in function table with
  generic families, the conversion rules; unknown types are left alone.

GL compilation and the program cache are in `../gl/`.
