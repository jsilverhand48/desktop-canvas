# src/scene/model/

Specification and implementation of a scene's description: project.json,
scene.json, and the model, material and effect files its objects name.
Evidence tags as in `src/scene/README.md`. Corpus numbers come from the
user's library (203 scenes) measured 2026-10-30 with
`claude_tests/corpus/model_survey.py` and `binding_survey.py`. Rendering
semantics (what a field does on screen) belong to later phases; this
module reads, validates and resolves values.

## 1. Reading JSON

All files are UTF-8 JSON. Observed [corpus, every project, scene, model,
material and effect file of 203 scenes]: no byte order mark, no NaN or
Infinity constants, no duplicate keys, no parse failure; nesting at most 13
levels; scene.json at most 1.5 MB (median 5 KB).

Reader rules (this project): a leading byte order mark is skipped; files
above 64 MiB or nested deeper than 64 levels are refused before parsing;
anything else that is not strict JSON (comments, trailing commas, NaN) is
an error for that file. A refused or failed file makes the thing that
needed it unavailable (an object without its model, a pass without its
material) and is reported; it never stops the whole scene from loading
unless it is project.json or the scene file itself.

Type tolerance [req A1]: every field below lists the JSON types accepted.
A field of another type keeps its default and adds a warning to the
report. Unknown keys are ignored.

## 2. Values

### 2.1 Plain values

| Kind | Encoding | Evidence |
|---|---|---|
| bool | JSON boolean | [corpus] |
| number | JSON number; a string holding one number is accepted too | [corpus: text `padding`, names] |
| vecN | string of N numbers separated by spaces, `"0.5 1 0"` | [corpus: `origin`, `scale`, `angles`, colours vec3; `size`, `parallaxDepth`, `cropoffset`, `spacing` vec2] |
| colour | vec3 of 0..1 floats | [corpus 234/234 colour properties] |
| string | JSON string | |
| path | string naming a file through the lookup of `../assets/README.md` section 2 | |

Vector parsing (this project): components are separated by spaces or
commas (2 effect constants are written "0.0, 1.0" [corpus]) and read left
to right; fewer than N fill from the field's default, more than N are
ignored (at most 4 seen [corpus]); a JSON
number or a one number string sets every component (the uniform rule of
section 2.3). A component that is not a number makes the whole value
invalid (default plus warning). `null` anywhere means absent [corpus:
`bloomstrength` 8, `bloomthreshold` 9, `clearenabled` 34, `instanceoverride`
158 are null].

### 2.2 Wrapped values

Any value may instead be an object holding the plain value under `value`
plus one or more of `user` (a user property binding, section 2.3),
`script` and `scriptproperties` (a SceneScript, phase P6a) and `animation`
(a timeline animation, phase P4b) [corpus]:

| Keys | Count |
|---|---|
| user, value | 1415 |
| script, value | 317 |
| script, scriptproperties, value | 236 |
| animation, value | 47 |
| script, user, value | 32 |
| animation, script, value | 18 |
| script, scriptproperties, user, value | 2 |

The model keeps `value` as the stored default and the script text, script
properties and animation JSON unevaluated; the report lists `script` and
`timeline` features for them. Values wrapped this way occur in scene
general settings, object fields, effect constants, particle instance
overrides and animation layers [corpus].

### 2.3 User property bindings

`"user": "name"` binds the value to project property `name` (section 3)
[doc: User Properties, "Bind User Property"]. Its current value (the
user's choice, else the property default) replaces `value`, converted to
the field's kind [corpus, binding_survey.py]:

| Property type | Field kind | Count | Rule |
|---|---|---|---|
| bool | bool | 369 | as is |
| color | vec3 | 235 | as is |
| slider | number | 130 | as is |
| textinput, text | string | 11 | as is |
| slider | vec3 | 10 | every component set to the slider value (all 10 bind `scale` and store "s s s" equal to the slider default) |
| slider | bool | 1 | non zero is true (default 15 bound to `visible`, stored true) |
| bool | number | 2 | true 1, false 0 |
| combo | any | | the option value as text, converted like a string |

Other pairs convert through the property's text form (section 3.2) parsed
as the field kind; if that fails the stored value stays and a warning is
added.

`"user": {"name": "name", "condition": "c"}` is a conditional binding: the
value is true exactly when the property's current value equals `c`
(compared as text, or as numbers when both are numeric; a number field
gets 1 or 0). All 42 such bindings with a known property name a
combo and a condition equal to one of its option values, around a bool
(layer, effect or animation layer visibility) [corpus]; this is the combo
property's "associate the layer with an option" feature [doc: Combo Box
User Property]. A binding naming no property (26 plain, 12 conditional
[corpus]) or `"user": null` (particle overrides, 8 [corpus]) leaves the
stored value.

## 3. project.json

| Field | Type | Notes |
|---|---|---|
| `file` | path | the scene file, always present [corpus 203/203] |
| `type` | string | `scene` 188, `Scene` 15: compared case insensitively [corpus] |
| `general.properties` | object | user properties by name, section 3.1 |
| `general.supportsaudioprocessing` | bool | 54 [corpus] |
| `workshopid` | string or number | a decimal id (string 55, number 8), 2 others [corpus]; used for the overrides of `../assets/README.md` section 2.1 |
| `title`, `description`, `preview`, `contentrating`, `tags`, `version`, `visibility`, `approved`, ... | | not needed for rendering |

### 3.1 User properties

Each entry of `general.properties` is an object keyed by the property's
name; bindings and conditions use that key [doc: User Properties: "the
auto-generated key is clock with a lowercase c"].

| Field | Type | Notes |
|---|---|---|
| `type` | string | lower cased; see the table below; missing in 49 entries, which are labels [corpus] |
| `text` | string | the label shown, possibly a `ui_` localisation key |
| `order` | number | display order [corpus 657/657] |
| `index` | number | 467 of 657 [corpus]; kept, not used |
| `value` | per type | the default |
| `options` | list of `{label, value}` | combo; values are strings or numeric strings [corpus 59 / 20] |
| `min`, `max`, `step`, `precision`, `fraction` | number, number, number, number, bool | slider [corpus 64, 64, 59, 59, 64] |
| `condition` | string | display condition, section 3.3 |

| Type | Default `value` | Count [corpus] |
|---|---|---|
| `bool` (checkbox) | boolean | 257 |
| `color` | colour string | 234 |
| `slider` | number | 64 |
| `combo` | option value string | 11 |
| `textinput` | string | 20 |
| `text` | none (a heading; bool or string seen) | 12 |
| `group` | none (a heading; string seen) | 7 |
| `scenetexture` | path string | 3 |
| `texture`, `usershortcut` | | 0; listed by [doc: User Properties] |
| (missing) | none (a label) | 49 |

### 3.2 Property values as text

Values cross process boundaries as text: the daemon's settings, the
helper's `--set-property NAME=VALUE`, the control channel's `props`
message. The text form: booleans `true`/`false` (`1`/`0` accepted on
input), numbers in decimal, colours `r g b`, strings as is. This is the
format `src/source/SceneOptions` already uses, and one parser serves both
(section 7).

### 3.3 Display conditions

A property's `condition` decides whether the settings UI shows it; it does
not affect rendering [doc: User Properties: "a JavaScript-compatible if
condition that decides when a property is visible to a user", examples
`show_clock.value == true`, `yourproperty.value == "example123"`]. Forms
seen, with P a property name, N a number and S a quoted string [corpus,
125 conditions]: `P.value == N` (73, three without spaces), `P.value ==
true` (16), `P.value == S` (12), `P.value == true && P.value == true`
(11), `P.value` (7), `P.value==false` (3), `P.value == S && P.value == S`
(3).

Evaluator (this project; never JavaScript): literals `true`, `false`,
numbers, single or double quoted strings; references `name.value`;
operators `==`, `!=`, `===`, `!==`, `&&`, `||`, `!` and parentheses.
Equality follows the loose rule the examples need: numbers and numeric
text compare as numbers, booleans compare with booleans (or as 1 and 0
against numbers), anything else as text. A bare reference is true when the
value is true, non zero or non empty text. An unknown name is undefined
(false, equal to nothing). An expression that does not parse shows the
property.

The daemon's settings UI evaluates each condition against the wallpaper's
pinned values (defaults otherwise) and hides properties whose condition
is false, keeping their pins; they appear once the values they depend on
are saved.

## 4. scene.json

| Field | Type | Notes |
|---|---|---|
| `camera` | `{center, eye, up}` vec3 | [corpus 203/203] |
| `general` | object | section 4.1 |
| `objects` | list | section 5, in drawing order |
| `version` | number | 108 [corpus] |

### 4.1 General settings

[corpus, count of 203 scenes; types besides the plain one are wrapped
values]: `ambientcolor` vec3 203, `skylightcolor` vec3 203, `clearcolor`
vec3 203 (bound 1), `clearenabled` bool 196 (null 34), `bloom` bool 203
(bound 4), `bloomstrength` number 201 (null 8, scripted 2, animated 1),
`bloomthreshold` number 201 (null 9, animated 1), `bloomtint` vec3 61,
`hdr` bool 108, `bloomhdrfeather`, `bloomhdrscatter`, `bloomhdrstrength`,
`bloomhdrthreshold` number 108 and `bloomhdriterations` 91,
`orthogonalprojection` `{width, height}` numbers 203, `zoom` number 127,
`fov`, `nearz`, `farz` number 162, `perspectiveoverridefov` 60,
`camerafade` bool 202, `camerapreview` bool 197, `cameraparallax` bool
197 with `cameraparallaxamount`, `cameraparallaxdelay`,
`cameraparallaxmouseinfluence` numbers, `camerashake` bool 197 with
`camerashakeamplitude`, `camerashakeroughness`, `camerashakespeed`,
`gravitydirection` vec3 and `gravitystrength` 56, `windenabled`,
`winddirection` vec3, `windstrength` 56, `lightconfig` `{point,
pointshadow}` 1.

The model keeps each as present or absent; the phase that renders a
setting fixes its default.

## 5. Objects

Common fields [corpus]: `id` number (always), `name` string, `parent`
number (an object id), `origin`, `scale`, `angles` vec3, `visible` bool,
`parallaxDepth` vec2, `attachment` string (on images 10 and particles 11
[req A1]), `dependencies` list of ids, `locktransforms`,
`disablepropagation`, `solid`, `castshadow`, `perspective` bool. Absent
transform and appearance fields take the identity: origin 0 0 0, scale 1
1 1, angles 0 0 0, visible true, alpha 1, colour 1 1 1 (this project).
Angles are stored in radians: `shape` quads hold z angles 0.41952 and
1.54700 next to editor round numbers elsewhere [corpus; to be checked by
the comparison of `../render/README.md` section 7].

The kind is the first of `image`, `particle`, `text`, `sound`, `light`
whose value is not null (particles carry `"image": null`), else `camera`,
`shape`, a non null `model`, else a group [corpus: 647 image, 812
particle, 204 text, 41 sound, 3 light, 1 camera, 4 shape, 4 group, 0
model].

| Kind | Fields [corpus] |
|---|---|
| image | `image` path of a model file (section 6), `size` vec2, `alpha`, `color`, `brightness`, `colorBlendMode` number, `alignment` (center, left, topleft, right, bottom), `copybackground`, `ledsource`, `clampuvs`, `nointerpolation` bool, `config.passthrough` bool, `effects` (section 5.1), `animationlayers` (section 5.2), `instance` `{id, combos, textures, usertextures}` (9, on instanced solid layers) |
| particle | `particle` path, `instanceoverride` (null or `{alpha, brightness, color, colorn, count, lifetime, rate, size, speed, controlpoint1..7, controlpointangle1, id}`, values wrapped as usual) |
| text | `text` string (scripted 131, bound 11 of 204), `font` path, `pointsize` number, `color`, `alpha`, `brightness`, `horizontalalign` (center, left, right), `verticalalign` (center, top), `anchor` (none, left, right), `padding` number or "x y" [req A1: 167 / 37], `spacing` vec2, `size` vec2, `maxrows`, `maxwidth` number, `limitrows`, `limitwidth`, `limituseellipsis`, `blockalign`, `opaquebackground` bool, `backgroundcolor` vec3, `backgroundbrightness` number, `effects` |
| sound | `sound` list of paths, `playbackmode` (loop 40, single 1), `volume`, `mintime`, `maxtime` number, `startsilent`, `muteineditor` bool |
| light | `light` (point, lpoint), `color`, `intensity`, `radius`, `exponent`, `density`, `volumetricsexponent`, `cascadedistance0..2`, `castshadow` |
| camera | `camera` ("default"), `path` (a camera path JSON), `queuemode`, `fov`, `zoom`; origin animated |
| shape | `shape` ("quad"), `effects` |
| group | none of the above; children point at it with `parent` |

### 5.1 Effect instances

`effects` lists `{file, id, name, username, visible, passes}` [corpus
1357 on images, texts and shapes]: `file` is an effect file path (section
8); `visible` a bool (bound 127, conditional 8, scripted 3 of 1328);
`passes` a list matching the effect's passes by position, each `{id,
combos, constantshadervalues, textures, usertextures}`. `combos` maps
combo names to numbers; `constantshadervalues` maps a shader constant's
material name (the `material` annotation of its uniform, phase P3) to a
wrapped value; `textures` lists paths or null (keep the effect's own).

### 5.2 Animation layers

`animationlayers` on puppet images [req A1; corpus 60 in 20 objects]:
`{id, name, animation, rate, blend, visible, additive, blendin, blendout,
blendtime}`. Defaults when absent: rate 1, blend 1, visible false,
additive false (blendin, blendout false, blendtime 0, this project).
`visible` is a wrapped bool (bound 17, conditional 6, scripted 3 of 60).

## 6. Model files

`models/*.json` an image names [corpus 647]: `material` path (always),
`autosize`, `fullscreen`, `passthrough`, `solidlayer`, `nopadding`,
`instanced`, `projectlayer` bool, `width`, `height` number (8),
`cropoffset` vec2 (33), `puppet` path of an MDL file (23, phase P8).

## 7. Material files

`{passes: [...]}`, one pass for object materials [corpus 647] and per
effect pass [corpus 1784]. Pass fields: `shader` name (the program in
`shaders/<name>.frag` and `.vert`; genericimage2 242, genericimage4 204,
flat 98, passthrough 43, composelayer 43, genericimage3 14, workshop
shaders), `blending` (translucent 644, normal 2 on objects; normal on all
effect passes), `cullmode` (nocull, normal), `depthtest`, `depthwrite`
(disabled), `alphawriting`, `textures` list of paths or null, `combos`,
`constantshadervalues`, `usershadervalues` (uniform name to property
name), `usertextures` (property names).

## 8. Effect files

`effects/<name>/effect.json` [corpus 1357 loads]: `name`, `group`,
`version`, `dependencies` (paths), `fbos` (`{name, format, scale, width,
height, unique, uvs}`; format rgba_backbuffer 155, rgba8888 116, r8 1),
`passes` (`{material, target, bind: [{name, index}], command, source,
compose}`; command `copy` 3), and editor only `gizmos`, `preview`,
`description`, `editable`, `performance`, `replacementkey`.

## 9. Capability report

`desktop-canvas-scene --check DIR` prints one JSON object: `wallpaper`,
`supported` (true when nothing is unsupported), `features` (what the
scene uses), `unsupported` (features this build cannot render), `errors`
(files that failed, section 1) and `warnings` (section 1 type tolerance),
and exits 0 when supported, 3 otherwise. Feature names: `scene` (camera,
clear colour and projection: every scene), `image`, `effects`, `solid-layer`, `fullscreen-layer`, `composition-layer`,
`shape-layer`, `group`, `particles`, `text`, `sound`, `light`, `puppet`,
`camera-path`, `3d-model`, `script`, `timeline`, `user-properties`,
`parallax`, `camera-shake`, `bloom`, `hdr`, `video-texture`,
`sprite-animation` (a texture with sprite sheet frames),
`particle-controlpoints`, `particle-children`, `particle-trails`
(particle systems using what phase P5b brings,
`../particles/README.md` section 7). A build
supports the features of the phases it contains; this one (phase P4a)
supports `scene`, `image`, `effects`, `solid-layer`, `fullscreen-layer`,
`composition-layer`, `group`, `user-properties`, `bloom` and `hdr`
(drawn as users without Ultra post-processing see it, `../render/README.md`
section 5) from phase P4a, `timeline`, `parallax`, `camera-shake`,
`sprite-animation` and `video-texture` (with libmpv only) from P4b,
`particles` from P5a, `particle-controlpoints`, `particle-children` and
`particle-trails` from P5b, and `text` from P6b, so the "auto" engine
uses the native renderer only for scenes made of those and keeps
linux-wallpaperengine for the rest.
`--check-all ROOT` runs the check on every scene folder in ROOT and
prints a summary (scenes, fully supported, feature counts), or a JSON
array with `--json`.

## 10. Shared property code

`UserProperties` (project.json properties, value text, conversions,
display conditions) is its own library, `canvas_scene_props`, with no
dependency beyond the JSON header, so the daemon (`src/source/
SceneOptions`) and the helper share it without the daemon linking the
scene engine.

## Files

`UserProperties` is library `canvas_scene_props`; the rest is part of
`canvas_scene_core` (namespace `canvas::scene` throughout).

- `UserProperties` section 3: declarations (`parseUserProperties`,
  `readUserProperties`), the text form (`valueText`, `textToBool`,
  `textToNumber`, `textToFloats`), `defaultValues` and
  `evaluateCondition` (a small recursive descent parser, never
  JavaScript).
- `Json` section 1: `parseJson` with the byte order mark, size and depth
  rules (`jsonNesting` counts brackets outside strings).
- `LoadReport.h` errors, warnings (capped at 200, then counted).
- `Field` section 2: `Field<T>` (stored and current value, presence,
  binding, unevaluated script and animation), `readPlain`/`readText`
  conversions for bool, float, int64, string, Vec2 to Vec4 and Floats,
  `readField`/`readValue` for wrapped values, `resolve` for bindings.
  Structs expose `forEachField(f)` so resolution and feature scans walk a
  scene without stored pointers.
- `Project` section 3: `ProjectFile` and `parseProjectFile`.
- `SceneFile` sections 4 and 5: camera, general settings, objects by kind,
  effect instances, animation layers, particle overrides.
- `Resources` sections 6 to 8: model, material and effect files.
- `SceneLoader` everything above through an `AssetLocator`: each path
  parsed once and shared, referenced particle, sound and texture files
  checked (texture headers parsed: payload kind for video textures, format
  code for the shaders' TEX<n>FORMAT), bindings resolved against
  defaults plus overrides; `applyProperties` for live changes;
  `texturePath` maps material texture names to `materials/<name>.tex`
  (every texture the 203 scenes reference resolves this way [corpus]).
- `Capabilities` section 9: `sceneFeatures`, `supportedFeatures` (empty
  until P4), `checkReport`.

Measured with these readers on 2026-10-30: all 203 scenes load with no
file error and no warning; the acceptance scene 3784295445 loads cleanly
[req A1].
