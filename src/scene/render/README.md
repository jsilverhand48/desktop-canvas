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

Output mapping (desktop-canvas option `--scaling`, the names lwe uses):
`fill` (default) scales the view uniformly to cover the output and crops
the overflow equally on both sides; `fit` scales it to fit inside and
fills the rest with the clear colour; `stretch` scales each axis to the
output. The frame is rendered at the output's pixel size, so nothing is
upscaled from the projection's size.

GL orientation: texture rows are stored top first, so texture coordinate
v 0 is the top of an image; render targets follow GL (row 0 at the
bottom), and passes that fill a target map texture coordinate (0,0) to
its bottom left, as Wallpaper Engine's own GL path does: its shaders flip
only under `#ifdef HLSL` [asset: composelayer.vert, genericimage2.vert].
(This corrects `../shader/README.md` section 1: `ddy` is negated because
Direct3D's screen Y runs down, not because of an upside down render.)

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
  Support], runs its effects and draws the result back
  [asset: composelayer.vert fills its own target];
- fullscreen layer (`fullscreen` and `passthrough`, shader `passthrough`
  with `_rt_FullFrameBuffer`): the same over the whole view.

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
   size; with `copybackground` [corpus: 248 layers set it] the background
   under the layer is composed beneath it first, opaque, as Wallpaper
   Engine's `effectcomposebackground` does [asset; inference on when];
2. each visible effect, in order, runs its passes [asset: effect.json]:
   `fbos` declare named buffers at the layer size divided by `scale`
   (formats rgba_backbuffer like the frame, rgba8888, r8); a pass draws
   its material into its `target`, or without one into the effect's
   output, which becomes `previous` for the next effect; `bind` puts named
   buffers or `previous` (the effect's input) into texture slots;
   `command` "copy" copies `source` to `target` [corpus: motionblur];
3. the result takes the place of the layer texture in the layer's own
   material passes, drawn with the layer's transform.

In effect passes `g_ModelViewProjectionMatrix` maps the layer onto the
pass target and `g_EffectModelViewProjectionMatrix` onto the screen
[asset: effectcomposebackground.vert].

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

The scene draws into an offscreen frame at the output's pixel size
(RGBA8; RGBA16F with `hdr`, phase P4a's bloom step), cleared to
`clearcolor` unless `clearenabled` is false [corpus: true 162, null 34,
never false]. `_rt_FullFrameBuffer` is a copy of the frame as drawn so
far, made right before a pass that samples it. The frame then scales
onto the output (section 1). With several outputs showing one scene, the
scene renders once per distinct (pixel size, scaling) and every such
output presents that frame.

Bloom and HDR (`bloom`, `hdr`, Wallpaper Engine's `materials/util`
bloom, downsample and combine materials) are specified with their step.

Engine uniforms set for every pass: the matrices of section 3,
`g_Time` (seconds since the scene started), `g_Texture<n>Resolution`,
`g_Screen` (frame width, height, aspect), `g_TexelSize` (1 / frame
size) and `g_TexelSizeHalf`, `g_PointerPosition` (normalized pointer),
the audio spectra (zeros until phase P7), `g_Color4`, `g_Color`,
`g_Alpha` [doc: Variables]. Unannotated uniforms GL would leave at zero
(`g_Color4`) are always set.

## 6. Tools

`desktop-canvas-scene --screenshot` renders the scene headless;
`--render-debug` options and `--window` arrive with later steps.

## 7. Comparison

Ground truth: every workshop item ships a preview image that Wallpaper
Engine rendered when the author published it [corpus: preview.jpg 170,
preview.gif 32, preview.png 1]. The comparison renders each scene
headless at the preview's aspect and compares downscaled images
(structural similarity and colour difference), alongside lwe screenshots
taken through the black box wrapper. Scores are recorded for every scene;
images are looked at only for scenes rated Everyone, and never for
3826693905.

## Files

(added with the implementation)
