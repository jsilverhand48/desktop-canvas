# src/scene/script/

Specification of SceneScript, Wallpaper Engine's JavaScript for scene
wallpapers (plan phase P6a): the runtime, how scripts are found, run and
fed, the globals and the layer objects scripts see. Evidence tags as in
`../README.md`; [doc] here also covers the official type declarations
`lib.sceneScript.d.ts` (version 2.8, linked from the SceneScript
reference). Corpus numbers come from `claude_tests/corpus/script_survey.py`
over the workshop scenes of 2026-11-01: 365 scripts in 55 scenes.

## 1. The runtime

QuickJS-ng runs the scripts: one runtime and context per scene, every
script an ES module ("Property scripts are ES modules" [req A8]). Before
any script, the context evaluates Wallpaper Engine's own
`scripts/jsclasses/baseclasses.js` from the assets folder [asset]: it
defines the classes `Vec2`, `Vec3`, `Vec4`, `Mat3`, `Mat4`,
`MediaPlaybackEvent`, `IModelData`, the function `createScriptProperties`
and the helpers `_Internal.updateScriptProperties`,
`_Internal.convertUserProperties` and `_Internal.stringifyConfig` the
engine uses to talk to scripts. These classes are Wallpaper Engine's own
code, so their behaviour is the original's (for example `new Vec2(v)`
copies the first two parts of a `Vec3`, where [req A8] asked for a
TypeError; the asset wins). Imports of `WEMath`, `WEColor` and
`WEVector` load `scripts/jsmodules/wemath.js`, `wecolor.js` and
`wevector.js` [asset; doc: Modules; corpus: 12, 7 and 2 imports]; any
other import fails the importing script.

Limits (this project): 64 MiB of memory per scene, 1 MiB of stack, and
an interrupt that stops any one call after 100 ms (a script stuck in a
loop is stopped and reported, the others go on).

## 2. Scripts

A script sits on a field of the scene file: the wrapped value's
`script` text, with optional `scriptproperties` [corpus: on text 145,
visible 45, origin 32, particle `rate` 25, alpha 23, effect `strength`
21, scale 16, color 13, angles 10, and scene settings; 130 of 365 carry
script properties]. The script drives that field's value; its layer is
the object holding the field (`thisLayer`, `thisObject`), the scene for
scene settings [doc: "thisLayer: The layer this script has been loaded
on"; "thisObject: The object this script belongs to"].

Loading (in scene file order, objects first and each object's fields in
the model's order [inference]):
1. the module is compiled and evaluated with `thisLayer` and
   `thisObject` set [req A8]; `engine.registerAudioBuffers()` and
   `createScriptProperties()` belong to this top level [doc];
2. an exported `scriptProperties` (the builder's `finish()` result)
   receives the field's `scriptproperties` through
   `_Internal.updateScriptProperties(module namespace, JSON)` [asset:
   the helper takes a script object with `scriptProperties` and a JSON
   text of values; Vec3 values are rebuilt as Vec3].
A script that fails to compile, load or run is logged once and skipped;
the others keep running [req A8].

Every frame, before the scene is laid out [inference: a script's
`origin` must move its layer in the same frame]:
- first frame: `init(value)`, then `applyUserProperties(all)` if
  exported (its return ignored), then `update(value)` [req A8; doc:
  init "called once after the object it belongs to has been created",
  update "called every frame"];
- later frames: `update(value)`;
- arguments are detached copies of the field's current value; a
  returned value replaces it, `undefined` changes nothing, `null` resets
  the field to its scene file value, booleans, numbers and strings are
  taken as the field's type allows, objects with numeric `x`, `y` (`z`,
  `w`) as vectors, anything else is ignored [req A8].
Before each call `thisLayer` and `thisObject` are the running script's
own [req A8]. Timers (section 3) run after the updates.

User properties: `applyUserProperties` gets every user property at the
start and, whenever properties change (a live change from the daemon),
the changed ones [doc: "Contains up-to-date values for user
properties", the parameter is `changedUserProperties`; plan: the gap
"applyUserProperties re-sent on every property change"], converted by
`_Internal.convertUserProperties` (colours as `Vec3`, user shortcuts as
objects) [asset]. `destroy()` runs when the scene unloads [doc].
`resizeScreen(size)` runs when the output size changes [doc], with the
output size in pixels. Media events (`mediaStatusChanged` and the
others) never fire: there is no media integration before phase P7c
[corpus: 70 scripts export one].

## 3. Globals

`engine` [doc: IEngine]: `frametime` (the frame's seconds), `runtime`
(scene seconds), `timeOfDay` (0..1 local time), `canvasSize` (the
projection size), `screenResolution` (the output size), `userProperties`
(every user property, converted as above), `isRunningInEditor()` false,
`isWallpaper()` true, `isScreensaver()` false, `isDesktopDevice()` true,
`isMobileDevice()` false, `isPortrait()` and `isLandscape()` from the
output size, `AUDIO_RESOLUTION_16`, `_32`, `_64`, `registerAudioBuffers(n)`
returning an object with `left`, `right` and `average` Float32Arrays of n
values, all zero until phase P7 feeds them [corpus: 94 scripts register],
`setTimeout(fn, ms)` and `setInterval(fn, ms)` returning a function that
cancels them [doc: "Returns a new callback that can be used to stop
the timeout"], `openUserShortcut()` false, `registerAsset()` null [corpus:
unused]. Reads return copies; writes to engine members are ignored [req
A8].

`thisScene` [doc: IScene]: `getLayer(name or index)` (index in scene
file order, out of range or unknown: undefined [req A8]),
`getLayerByID(id)`, `getLayerCount()`, `enumerateLayers()`,
`getLayerIndex(layer)`, `getInitialLayerConfig(layer)` (a copy of the
object's scene file entry), `sortLayer(layer, index)` (moves the layer
in drawing order), the scene settings `bloom`, `bloomstrength`,
`bloomthreshold`, `clearcolor`, `clearenabled`, `camerashake`,
`camerashakeamplitude`, `camerashakeroughness`, `camerashakespeed`,
`cameraparallax`, `cameraparallaxamount`, `cameraparallaxdelay`,
`cameraparallaxmouseinfluence` (read and write), `getAnimation(name)`.
`createLayer` and `destroyLayer` (5 corpus scripts) return undefined
and false: not done.

`input` [doc: IInput]: `cursorWorldPosition` (the cursor in scene
units, `../render/README.md` section 11's mapping), `cursorScreenPosition`
(output pixels), `cursorLeftDown`.

`console.log` and `console.error` write to the helper's log, at most
50 lines per script per minute [this project]. `shared` is one empty
object for all of a scene's scripts [doc]. `localStorage` [doc:
ILocalStorage]: `get`, `set`, `delete`, `clear` with the locations
`LOCATION_GLOBAL` and `LOCATION_SCREEN` (one store per wallpaper, and
per wallpaper and output), kept as JSON under the helper's local
storage directory (`$XDG_DATA_HOME/desktop-canvas/localstorage/`) and
written when changed [inference: the names say where values are shared].

## 4. Layer objects

`thisLayer`, `thisObject` and what `getLayer` returns are one object per
scene object [doc: ILayer and the interfaces it extends]. Reads return
copies; writes change the object's fields at once (the renderer reads
them every frame); unknown names are ignored [req A8]. Members [doc;
req A8; corpus: the members scripts use]:
- every layer: `name` (read only), `origin`, `angles`, `scale`,
  `visible`, `parallaxDepth`, `getParent()`, `getChildren()`,
  `getAnimation(name)`;
- images: `alpha`, `color`, `alignment`, `size` (read only),
  `getEffect(name or index)`, `getEffectCount()`,
  `getTextureAnimation()`;
- text: `text`, `color`, `alpha`, `pointsize`, `font`, `padding`,
  `horizontalalign`, `verticalalign`, `anchor`, `limitrows`, `maxrows`,
  `limitwidth`, `maxwidth`, `opaquebackground`, `backgroundcolor`,
  `size` (read only, the measured box), `getEffect`, `getEffectCount`;
- particles: `instance` (`alpha`, `size`, `count`, `speed`, `lifetime`,
  `rate`, `colorn`, `controlpoint0` to `controlpoint7`: the layer
  overrides of `../particles/README.md` section 2), `play()`, `pause()`,
  `stop()`, `isPlaying()`;
- effects (`getEffect`): `name` (read only) and `visible`.
`getAnimation(name)` gives a timeline animation (`IAnimation`: `play`,
`pause`, `stop`, `isPlaying`, `getFrame`, `setFrame`, `rate`, `fps`,
`frameCount`, `duration`, `name`) of the object's animated fields
[corpus: 18 `thisObject.getAnimation`]; `getTextureAnimation()` the
same controls over the layer's sprite sheet. Puppet bones, attachments,
video textures, sound layers and 3D models are later phases.

## 5. Cursor events

The cursor is over a layer when it lies inside the layer's rectangle
after its transforms (images and text: their quad; others: never)
[doc: "when the cursor enters the bounds of the object"]; of several
layers, the topmost drawn. Scripts of that layer receive `cursorEnter`,
`cursorLeave`, `cursorMove`, `cursorDown`, `cursorUp` and, for a down
and up on the same layer, `cursorClick`, with a `CursorEvent`:
`worldPosition` (scene units) and `localPosition` (in the layer's own
space) [doc: CursorEvent]. Buttons come from the pointer button events
the daemon forwards; without them only enter, leave and move fire.

## 6. Capability

`script` (`../model/README.md` section 9) is supported from this phase.

## Files

(added with the implementation)
