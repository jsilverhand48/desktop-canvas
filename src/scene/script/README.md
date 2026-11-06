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

Once per frame advance (outputs drawing the same advance share one run),
after the timeline animations and before the scene is laid out
[inference: a script's `origin` must move its layer in the same frame]:
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
output size in pixels as a `Vec2`, from the second frame on (the first
size is no change). Media events (`mediaStatusChanged` and the
others) fire from phase P7c as `../audio/README.md` section 7 describes
[corpus: 70 scripts export one]. Nor does `applyGeneralSettings`: its
only setting is the editor's language [doc].

## 3. Globals

`engine` [doc: IEngine]: `frametime` (the frame's seconds), `runtime`
(scene seconds), `timeOfDay` (0..1 local time), `canvasSize` (the
projection size), `screenResolution` (the output size), `userProperties`
(every user property, converted as above), `isRunningInEditor()` false,
`isWallpaper()` true, `isScreensaver()` false, `isDesktopDevice()` true,
`isMobileDevice()` false, `isPortrait()` and `isLandscape()` from the
output size, `AUDIO_RESOLUTION_16`, `_32`, `_64`, `registerAudioBuffers(n)`
returning an object with `left`, `right` and `average` Float32Arrays of n
values, filled before every frame's calls from the spectrum
(`../audio/README.md` section 1; zero without capture) [corpus: 94
scripts register],
`setTimeout(fn, ms)` and `setInterval(fn, ms)` returning a function that
cancels them [doc: "Returns a new callback that can be used to stop
the timeout"], `openUserShortcut()` false, `registerAsset()` null [corpus:
unused]. Reads return copies; writes to engine members are ignored [req
A8].

`thisScene` [doc: IScene]: `getLayer(name or index)` (index in scene
file order, out of range or unknown: undefined [req A8]),
`getLayerByID(id)`, `getLayerCount()`, `enumerateLayers()`,
`getLayerIndex(layer)`, `getInitialLayerConfig(layer)` (a copy of the
object's scene file entry), the scene settings `bloom`, `bloomstrength`,
`bloomthreshold`, `clearcolor`, `clearenabled`, `camerashake`,
`camerashakeamplitude`, `camerashakeroughness`, `camerashakespeed`,
`cameraparallax`, `cameraparallaxamount`, `cameraparallaxdelay`,
`cameraparallaxmouseinfluence` (read and write), `getAnimation(name)`.
`createLayer`, `destroyLayer` and `sortLayer` return undefined, false
and false: not done. Five corpus scenes create layers from a script (a
text layer from properties, image layers from `models/*.json` paths,
copies of `getInitialLayerConfig`), and other scripts of those scenes
look the new layers up by name, so the capability report lists a script
containing `createLayer` as the feature `script-layers`, which this
build does not support (the "auto" engine keeps those scenes on
linux-wallpaperengine).

`input` [doc: IInput]: `cursorWorldPosition` (the cursor in scene
units, `../render/README.md` section 11's mapping), `cursorScreenPosition`
(output pixels), `cursorLeftDown`.

`console.log` and `console.error` write to the helper's log, at most
50 lines per script per minute [this project]. `shared` is one empty
object for all of a scene's scripts [doc]. `localStorage` [doc:
ILocalStorage]: `get`, `set`, `delete`, `clear` with the locations
`LOCATION_GLOBAL` and `LOCATION_SCREEN` (one store per wallpaper, and
per wallpaper and output) [inference: the names say where values are
shared]. Both live in one JSON file per wallpaper,
`$XDG_DATA_HOME/desktop-canvas/localstorage/<wallpaper folder name>.json`
(`~/.local/share/...` without the variable), written in the frame they
change. One helper draws the same scene on every output of its group,
so those outputs share the screen store [this project]. Screenshots keep
nothing: a one-off run must not change what the wallpaper remembers.

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
  overrides of `../particles/README.md` section 2), and [doc:
  IParticleSystem] `play()` (a stopped system starts over: emission from
  the beginning, without the load time prewarm; a paused one emits
  again), `pause()` (no new particles, the live ones go on), `stop()`
  (every particle gone at once, nothing emitted), `isPlaying()` (true
  while emitting or while particles live); `emitParticles(count)` does
  nothing yet;
- effects (`getEffect`): `name` (read only) and `visible`.
`getAnimation(name)` gives a timeline animation (`IAnimation`: `play`,
`pause`, `stop`, `isPlaying`, `getFrame`, `setFrame`, `rate`, `fps`,
`frameCount`, `duration`, `name`) of the object's animated fields, the
first whose `options.name` matches, or the first at all without a name
[corpus: 18 `thisObject.getAnimation`]. Each animation runs on its own
clock (`../render/README.md` section 10): `stop` also rewinds it [doc:
"Stop the animation and reset the current time to zero"], and
`startpaused` ones wait on frame 0 until played [asset: the option's
name]. `getTextureAnimation()` returns controls that do nothing yet.
Puppet image layers add animation layers, bones and blend shapes, and
every layer the attachment calls (`../puppet/README.md` section 7,
phase P8). Video textures and 3D models are later phases.

## 5. Cursor events

The cursor is over a layer when it lies inside the layer's rectangle as
drawn: after its transforms and parallax, shown layers only (images and
text, text measured as drawn; others: never) [doc: "when the cursor
enters the bounds of the object"]; of several layers, the topmost
drawn. Scripts of that layer receive `cursorEnter`, `cursorLeave`,
`cursorDown`, `cursorUp` and, for a down and up on the same layer,
`cursorClick`; `cursorMove` reaches every script that exports it
whenever the cursor moves [doc: cursorMove is "called when the cursor
has been moved", where the others name the object]. The argument is a
`CursorEvent`: `worldPosition` (scene units) and `localPosition` (in
the layer's own space, before its scale) [doc: CursorEvent]. The left
button comes from the helper's own surfaces (Wayland compositors that
give the wallpaper layer pointer input, `../../display/README.md`);
where the helper sees no buttons (KDE through the KWin bridge, X11)
only enter, leave and move fire.

## 6. Capability

`script` (`../model/README.md` section 9) is supported from this phase
in builds with QuickJS-ng. CMake looks for its header and library and
checks that a program using them links; without that the build still
succeeds, `ScriptEngine::available()` is false, `script` stays
unsupported (auto mode falls back) and a scene's scripts do not run.

## Files

- `ScriptEngine.h`, `ScriptEngine.cpp`: the runtime. `ScriptEngine`
  loads WE's base classes and modules, this project's globals (a
  JavaScript prelude over one native dispatcher) and every script of a
  `LoadedScene`; `update(ScriptFrame)` runs one frame (property changes,
  cursor events, init, applyUserProperties, update, timers, pending
  jobs, localStorage). Layer objects read and write the model's fields;
  the renderer answers the rest through `ScriptHost` (layer sizes,
  particle playback), the `Animator` the animations. Console lines come
  out of `takeLog()`, failures out of `takeProblems()`. Without QuickJS
  the same API compiles to a stub whose `create()` fails.

The renderer side (`../gl/SceneRenderer.cpp`) creates the engine after
the layers when any field has a script, runs it once per advance and
hit tests the cursor; tests: `claude_tests/test_scene_script.cpp`
(needs QuickJS and WE's assets) and the playback part of
`claude_tests/test_scene_motion.cpp`.
