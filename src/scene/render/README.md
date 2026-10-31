# src/scene/render/

Specification of how a loaded scene becomes pixels: coordinates, layers,
effects, blending and the frame. Evidence tags as in `src/scene/README.md`,
plus **[inference]** for rules read from evidence that does not state
them; each inference is checked by the comparison of section 7. Corpus
numbers come from the 203 scenes (2026-10-31). The GPU free parts
(`Layout`) are in `canvas_scene_core`, the GL parts in `../gl/`.

## 1. Coordinates and the view

Scene units are pixels of `general.orthogonalprojection` `{width,
height}` [corpus 203/203]. The view spans x from 0 to width and y from 0
to height with Y up: every camera has `up` "0 1 0" [corpus 203/203], and
full size layers sit at (width/2, height/2) [corpus 180 of the 182 scenes
that have one]. The camera `center` and `eye` are the editor's view, not
the wallpaper's: 96 scenes keep arbitrary editor positions such as (45.1,
4.7) or (-242.4, 50.4) while their backgrounds sit exactly on the
projection centre [corpus; inference]. `orthogonalprojection.auto`
(Wallpaper Engine's own GIF scene [asset: scenes/gifs]) sizes the view to
the first image; no workshop scene uses it.

`zoom` above 1 magnifies around the view centre (the view spans
width/zoom) [inference]: 1 in 117 scenes, absent in 76, 1.0042 to 1.16 in
8 [corpus].

Angles are radians (`../model/README.md` section 5); only the Z angle
turns layers in these orthographic scenes.

Output mapping (desktop-canvas options `--scaling` and `--clamp`, the
names lwe uses): `fill` (default) scales the view uniformly to cover the
output and crops the overflow equally on both sides; `stretch` scales
each axis to the output; both render the frame at the output's pixel
size. `fit` renders the frame at the largest size of the projection's
aspect that fits, centres it, and fills the bars by `--clamp`: `clamp`
(default) stretches the edge pixels, `repeat` tiles the frame, `border`
shows the clear colour. Nothing is upscaled from the projection's size.

Buffer layout: texture rows are stored top first, so texture coordinate
v 0 is the top of an image, and every buffer, the frame included, holds
its content top row first, the way Direct3D lays out render targets.
Wallpaper Engine compiles its shaders as HLSL on desktop [doc: Mobile],
where the previews come from, and flips Y only under `#ifdef HLSL`
[asset: composelayer.vert, genericimage2.vert]; their GL text therefore
gives Direct3D's rows when every projection is followed by a Y flip
(clip y negated) and the finished frame is flipped once when presented.
Texture coordinate (0,0) then lands on row 0 of any target, and
`_rt_FullFrameBuffer` sampled at screen coordinates `clip * 0.5 + 0.5`
gives the pixel under the fragment. GL's `dFdy` runs along increasing
rows like Direct3D's `ddy` (`../shader/README.md` section 1).

## 2. Layers

Objects draw in `objects` order, the first at the bottom: the full size
background is the first object in 190 scenes [corpus]. Depth testing and
writing are off: every material says `disabled` [corpus].

World transform [req A3]: an object's matrix is translate(origin) *
rotateZ(angles.z) * scale(scale). Parent chains resolve for every object
type: the child's origin xy is multiplied by the parent's scale, turned by
the parent's Z angle and offset by the parent's origin; z = parent z +
child z * parent scale z; scales multiply and Z angles add; the walk stops
at a missing parent or after 32 levels.

Visibility [req A4]: image, text and particle layers draw only when their
own `visible` and every ancestor's are true (groups default to true),
evaluated every frame, never only at load.

Image layer rectangle: the object's `size` [corpus 607 of 647], else the
model's `width` and `height`, else the base texture's image size
(`autosize` [asset: models]). `alignment` [corpus: center 164, left 33,
topleft 24, right 1, bottom 1] names the point of the rectangle the
origin is at; absent means center [inference].

Texture region: Wallpaper Engine's image shaders sample `a_TexCoord`
directly [asset: genericimage2.vert], so the quad carries the image
region: u from 0 to image width / texture width and v likewise for padded
raw textures (`../assets/README.md` section 3.1); textures decoded from
JPEG or PNG are uploaded at their image size and use the whole range.
`g_Texture<n>Resolution` is (uploaded width, height, image width, height)
[doc: Variables].

Layer kinds by model flags [asset: models/util, materials/util]:
- image: the material's passes with the layer texture;
- solid layer (`solidlayer`): shader `flat`, `g_Color` and `g_Alpha`
  [asset: flat.frag];
- composition layer (`passthrough`, material `composelayer` sampling
  `_rt_FullFrameBuffer`): captures what is drawn under its rectangle,
  "like a camera that records a part of the scene" [doc: RGB Hardware
  Support], runs its effects and draws the result back. Its own pass is
  the capture: drawn with the layer's screen matrix, `composelayer.vert`
  fills the whole target from the texture coordinates and samples the
  frame where the layer is on screen [asset];
- fullscreen layer (`fullscreen` and `passthrough`, shader `passthrough`
  with `_rt_FullFrameBuffer`): the same over the whole frame; without
  `TRANSFORM` its positions are clip coordinates [asset:
  passthrough.vert], so its quad spans clip space with texture
  coordinate (0,0) on row 0.

Composition and fullscreen layers without visible effects are not drawn
(capturing and drawing back changes nothing). With effects, their result
and that of solid layers with effects goes back onto the frame over the
layer's rectangle with translucent blending [inference].

Per layer values [asset shaders; inference]: `g_Color4` is (color *
brightness, alpha), used by genericimage3 and genericimage4 and by
genericimage2 with its `VERSION` combo; genericimage2 otherwise reads its
`Brightness` and `Alpha` constants, which default to the object's
brightness and alpha; flat reads `g_Color` (color) and `g_Alpha`.
`colorBlendMode` sets the layer material's `BLENDMODE` combo
(genericimage2 blends against `_rt_FullFrameBuffer` through
`common_blending.h` [asset]); 19 layers use a mode other than 0 [corpus].

Shape layers (`shape` "quad", 4 objects) and particle, text, sound and
light layers belong to later phases.

## 3. Effects

Effects apply to image, text, fullscreen and composition layers [doc:
Introduction to Effects]. A layer with visible effects renders through
them:
1. the base pass draws the layer's texture into a buffer of the layer's
   size (its image region, texture coordinates 0 to the usable fraction;
   a sprite sheet's first frame through the `SPRITESHEET` combo); with
   `copybackground` [corpus: 248 layers set it] the background under the
   layer is composed beneath it first, opaque, as Wallpaper Engine's
   `effectcomposebackground` does [asset; inference on when]. (The
   "Copy background" option the Shine and God Rays pages describe [doc]
   is a different thing, those effects' `COPYBG` combo [asset].)
   Composition, fullscreen and solid layers use their own pass instead
   (section 2);
2. each visible effect, in order, runs its passes [asset: effect.json]:
   `fbos` declare named buffers at the layer size divided by `scale`
   (formats rgba_backbuffer like the frame, rgba8888, r8; scales 1, 2
   and 4 [corpus]), cleared before the effect runs, except `unique`
   buffers, which keep their pixels from frame to frame (motionblur's
   accumulation, the only one [corpus]); fbo names need not start with
   `_rt_` (`render_target`, `_downscaled1` [corpus: 2 effects]), so
   binds and targets match them by exact name; a pass draws its material into
   its `target`, or without one into the effect's output, which becomes
   `previous` for the next effect (every effect's last pass has no
   target; 2 effects have an earlier pass without one [corpus]); `bind`
   puts named buffers or `previous` (the effect's input) into texture
   slots; `command` "copy" copies `source` to `target` [corpus:
   motionblur]. Effect passes run with the layer rectangle mapped onto
   the whole target and texture coordinates 0 to 1, which is what
   effect shaders assume: they scale only other textures by
   `g_Texture<n>Resolution.zw / .xy` [asset: 77 effect vertex shaders];
3. the result takes the place of the layer texture in the layer's own
   material passes, drawn with the layer's transform and texture
   coordinates 0 to 1.

Base and effect passes see `g_Color4` white: the layer's colour and
alpha apply once, in its own passes [inference].

Base and effect passes draw a quad already in clip space (texture
(0,0) on row 0) with `g_ModelViewProjectionMatrix` the identity: some
effect vertex shaders write `a_Position` straight to `gl_Position`
(godrays_cast.vert and 53 more of the 173 in the assets; 209 package
files), others multiply it by `g_ModelViewProjectionMatrix` (109 assets
files, 471 package files) [asset; corpus], and both fill the target only
this way. `g_EffectModelViewProjectionMatrix` maps that quad onto the
screen where the layer is [asset: effectcomposebackground.vert], and
`g_ModelMatrix`/`g_EffectModelMatrix` onto the scene.

Layer composites: `_rt_imageLayerComposite_<id>_a` and `_b` name image
layer `<id>`'s two composite buffers [corpus: 67 distinct names in 46
scenes]. Every name a layer uses for itself (61) sits in a slot its
effect binds to `previous` (the combine passes of godrays, shine, blur
and localcontrast [asset, corpus]), so a self reference means
`previous`. Names of other layers (6, in 7 slots, all in the Blend and
Opacity effects of 3521337568's Earth) use that layer as a texture, as
the Blend effect allows ("drag & drop another image layer" [doc: Blend
Effect]): the referenced layer's result after its effects (its base
pass alone without effects), computed every frame even when the layer
is hidden (3521337568 hides its sources) [inference]. A name always
refers to the layer itself or an earlier one [corpus 67/67], so draw
order computes it first.

Texture slots of any pass, first found wins: an effect `bind`, the
scene.json instance's pass `textures`, the material's `textures`, the
sampler annotation `default`; slot 0 of an effect pass with none of them
is `previous`. Names starting `_rt_` are render targets (section 5).
Constants by material name, first found wins: the instance's pass
`constantshadervalues`, the material's, the uniform annotation's
`default`; bound and scripted values as in the model.

`compose` passes (4 scenes) are logged and drawn like other passes until
their meaning is known.

## 4. Blending

Material pass `blending` [corpus: object materials translucent 644 and
normal 2, effect passes all normal]:
- `translucent`: colour = src * src alpha + dst * (1 - src alpha), alpha
  = src alpha + dst alpha * (1 - src alpha) [inference];
- `normal`: no blending, the pass replaces what it covers;
- `additive`: src + dst [inference]; the rest (particle materials) in P5.

## 5. The frame

The scene draws into an offscreen frame at the size section 1 gives
(RGBA8; RGBA16F only with the experimental `ultra` switch below), cleared to
`clearcolor` unless `clearenabled` is false [corpus: true 162, null 34,
never false]. `_rt_FullFrameBuffer` is a copy of the frame as drawn so
far, made right before a pass that samples it (once until the frame
changes); `_rt_MipMappedFrameBuffer` is the same with mipmaps,
`g_Texture<n>MipMapInfo` its highest level. The frame then goes onto the
output (section 1), opaque. A frame is drawn once per tick: with several
outputs showing one scene, every output of the same frame size presents
that frame.

Bloom [asset: `materials/util` and `shaders` of the names below]: with
`bloom` true and `hdr` not (19 scenes, 4 of them binding `bloom` to a
property [corpus]), the finished frame goes through Wallpaper Engine's
own bloom materials, each drawn over clip space:
1. `downsample_quarter_bloom` reads the frame into a buffer a quarter of
   its size per axis (`_rt_4FrameBuffer`): four bilinear taps one frame
   texel off each target texel's centre average a 4x4 block; the part
   above `bloomthreshold` is kept, saturation raised, and the result
   multiplied by `bloomstrength` and `bloomtint` (annotation defaults
   0.65, 2 and 1 1 1; the scene's general values replace them);
2. `downsample_eighth_blur_v` blurs horizontally (13 taps, 8 frame
   texels apart) into an eighth size buffer (`_rt_8FrameBuffer`);
3. `blur_h_bloom` blurs vertically the same way into `_rt_Bloom`, also
   an eighth [inference: each material names only its input];
4. `combine_ldr` writes frame plus bloom, alpha 1, back as the frame.
`g_TexelSize` stays one frame texel throughout, which is what the taps
are written in [asset].

HDR (`hdr` true, 8 scenes [corpus]) is Wallpaper Engine's "Ultra"
post-processing: HDR bloom shows only for users who set post-processing
to Ultra, and "all users who only have normal bloom enabled will only see
normal bloom on your wallpaper"; authors are told to configure the normal
bloom settings for them [doc: Bloom Effects]. desktop-canvas renders
`hdr` scenes that way: an RGBA8 frame and the normal bloom chain above
with `bloomstrength` and `bloomthreshold`. The workshop previews come from
the editor, which always shows the HDR look [doc: Bloom Effects], so those
scenes compare darker.

The HDR chain itself is behind the experimental `--render-debug ultra`
switch while its behaviour is pinned down [asset: materials/util
hdr_downsample_bloom, hdr_downsample, hdr_upsample,
combine_hdr_upsample_linear; shaders hdr_downsample, combine_hdr]: the
frame and `rgba_backbuffer` buffers become RGBA16F; `hdr_downsample_bloom`
halves the frame into level 1 keeping what exceeds `bloomhdrthreshold`
with a soft knee (`g_BloomBlendParams` = (threshold, threshold - feather,
2 * feather, 0.25 / feather), feather `bloomhdrfeather` [inference: the
usual soft knee curve the shader's arithmetic implements; "threshold
smoothing ... if you set this to 1, then even dark part of the image will
glow very slightly" doc: Bloom Effects]) times `bloomhdrstrength` and
`bloomtint`; `hdr_downsample` halves level after level up to
`bloomhdriterations` (8 when absent); `hdr_upsample` (additive) adds
each level times `bloomhdrscatter` onto the next larger one; the
`LINEAR` combine adds level 1 to the frame. `g_RenderVar0` is (0.5 /
source width, 0.5 / source height, -0.5 / source width, -0.5 / source
height): the shader's bicubic path recovers the source size as 0.5 /
`g_RenderVar0.xy` [inference]. Open question: with scatter 2 and eight
levels this compounds to white in 2404179953, whose preview shows a mild
glow, so the levels must combine with less gain than this reading
gives.

Engine uniforms set for every pass: the matrices of section 3,
`g_Time` (seconds since the scene started), `g_Texture<n>Resolution`,
`g_Screen` (frame width, height, aspect), `g_TexelSize` (1 / frame
size) and `g_TexelSizeHalf`, `g_PointerPosition` (normalized pointer),
the audio spectra (zeros until phase P7), `g_Color4`, `g_Color`,
`g_Alpha` [doc: Variables]. Unannotated uniforms GL would leave at zero
(`g_Color4`) are always set.

## 6. Tools

`desktop-canvas-scene --screenshot` renders the scene headless.
`--render-debug LIST` (comma separated, repeatable) helps find which
layer or effect does what: `pass-log` logs every pass of the first
frame (where, target, blending, slot kinds), `base-only` runs no
effects and no bloom, `no-bloom` skips bloom, `ultra` turns on the
experimental HDR chain for `hdr` scenes (section 5), `no-solid-final` presents
the frame's alpha, `object=ID` draws
only the named objects, `skip-object=ID` leaves one out, and
`skip-effect=ID[:N]` skips an object's effect N or all of them, and
`dump-programs=DIR` writes every translated program (vertex, fragment,
and the pass and request that asked for it) into DIR. Objects
that others sample as composites are still computed.

`desktop-canvas-scene --window WxH WALLPAPER_DIR` shows the scene in an
ordinary window (xdg-shell on Wayland, a normal X11 window) instead of
wallpaper surfaces, with the pointer reported over it; closing the
window ends the helper. With `--render-debug` it is the interactive way
to look at a scene.

## 7. Comparison

Ground truth: every workshop item ships a preview image that Wallpaper
Engine rendered when the author published it [corpus: preview.jpg 174,
preview.gif 33, preview.png 1, all square]. Authors frame previews
themselves (centre crops, zooms on a face), so the comparison
(`claude_tests/corpus/compare_previews.py`) renders each scene headless
at its own aspect, 1280 pixels wide, and searches square crops of
several sizes and positions for the best structural similarity (grey,
64x64) with the preview; it also reports the centred square and the
colour difference of the best crop. lwe screenshots, taken through the
black box wrapper, can be scored the same way (`--lwe`; each opens a
window for a few seconds). Scores are recorded for every scene; images
are looked at only for scenes rated Everyone, and never for 3826693905.
Previews of `hdr` scenes show the HDR look (section 5), and previews of
animated scenes catch one moment, so the scores are a review aid, not a
gate.

## Files

- `Layout.h`, `Layout.cpp` (`canvas_scene_core`): matrices, world
  transforms through parents [req A3], visibility [req A4], layer
  rectangles, view projection.
- `../gl/Textures.h`, `../gl/Textures.cpp`: texture upload (section 2's
  texture region, the first sprite frame).
- `../gl/SceneRenderer.h`, `../gl/SceneRenderer.cpp`: sections 1 to 6.
- `../../scene-host/NativeRenderer.h`, `.cpp`: the renderer inside
  `desktop-canvas-scene`.
- Tests: `claude_tests/test_scene_render.cpp` (layout math, debug
  options, synthetic scenes drawn headless and checked pixel by pixel,
  one of them through the bloom chain); `claude_tests/corpus/render_survey.py` (the corpus numbers
  above) and `render_all.py` (renders every scene).
