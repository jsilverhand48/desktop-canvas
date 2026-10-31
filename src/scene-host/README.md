# src/scene-host/

`desktop-canvas-scene`, the native scene renderer helper. The daemon starts
it for scene wallpapers on the "native" and "auto" engines (see
`render/InstanceManager.h`), supervised like the web helper, and drives it
over the control channel protocol in `process/HelperProtocol.h`. It also
runs by hand for previews, screenshots and checks.

Status: plan phase P4a. The engine draws image, solid, composition and
fullscreen layers with their effects (`src/scene/render/README.md`);
particles, text, sound, scripts, animation and puppets arrive with later
phases. The support check reports scenes made only of what this build
draws as supported, so "auto" uses the native renderer for those and
linux-wallpaperengine for the rest; "native" draws what it can and skips
the rest. A scene that cannot be read, or has nothing drawable, shows a
solid dark slate placeholder.

Why a separate process: workshop content is untrusted (binary formats,
shaders, JavaScript); a crash or hang takes down only the helper, whose
memory is fully returned on exit, and loading or shader compilation never
blocks the daemon's single threaded event loop.

## Files

- `main.cpp` command line, mode dispatch, platform selection (Wayland, then
  X11) with a GL 3.3 core, robust, alpha-less EGL context, sandbox, then
  SceneHost. `--window WxH` shows the scene in an ordinary window instead
  of wallpaper surfaces. Modes and flags are listed in its header comment; exit codes
  follow HelperProtocol (0 clean, 2 usage, 3 unsupported, 1 failure).
  Without `--assets-dir` it looks for the Wallpaper Engine assets folder
  in the Steam library holding the wallpaper
  (`scene::AssetLocator::guessAssetsDir`).
- `SceneCheck` the capability report (`src/scene/model/README.md`
  section 9): `examine()` loads the scene model with the `--set-property`
  values and lists the features this build cannot render; it backs
  `--check` (JSON, exit 0 or 3), the `--strict` start (the "unsupported"
  message and exit 3 the "auto" engine relies on) and `--check-all ROOT`
  (summary, or a JSON array with `--json`; non scene folders skipped).
- `AssetTools` the inspection modes of `src/scene/assets/README.md`
  section 4.1. `--extract ENTRY OUT` resolves ENTRY through the lookup
  layers (logging which one answered); a `.tex` is decoded (first image,
  first mip, DXT expanded) and written as a PNG (R8 grayscale, RG88 as RGB
  with blue 0, else RGBA; volume slices stacked) with its header printed as
  JSON, anything else is copied. `--dump DIR` writes every scene.pkg entry
  below DIR, which must be empty or absent, never replacing a file. Both
  run sandboxed, writable only where they write.
- `SceneHost` everything around the pixels: one RenderSurface per wanted
  output (dropped when the output disappears, recreated when it returns or
  reports `closed()`), frame pacing, and the helper side of the protocol
  (hello, ready once every output presented, stats every 2 s as heartbeat;
  pause, resume, quit, mute, volume, pointer, props, covered). Pointer
  motion and buttons the helper sees itself (its own Wayland surfaces on
  wlroots, polling on X11) go to the renderer too; on Plasma the daemon
  forwards the KDE bridge's cursor as "pointer" messages. In window mode
  (`SceneHostOptions::windowWidth`, `windowHeight`) it makes one window
  through `Platform::createWindow` instead of output surfaces, ignores
  output changes, and ends the event loop cleanly when the window is
  closed. `--span`
  (`SceneHostOptions::span`) is accepted and logged; each output still
  renders the whole frame (spanning lands with the screen span step). No
  GL, so `claude_tests/test_scene_host` drives it with fakes.
- `FrameClock` fixed rate ticker on absolute deadlines: no drift, and no
  catch up burst after a stall (it restarts from now).
- `Renderer` the interface SceneHost drives: `advance(dt)` once per tick,
  then `render(width, height)` per output into the bound framebuffer, plus
  live pointer, property, mute and volume inputs.
- `NativeRenderer` the scene engine (`scene/gl/SceneRenderer`) behind
  that interface: `create()` reads the wallpaper and loads the scene
  before GL exists (and before the sandbox), the first `render()` builds
  the GL side; `--scaling`, `--clamp` and `--render-debug` reach the
  engine through it, and compiled programs are cached in
  `$XDG_CACHE_HOME/desktop-canvas/shaders`. GL objects are freed only by
  `shutdown()` with the context current (the screenshot path does so);
  otherwise the driver frees them at exit.
- `PlaceholderRenderer` clears to one colour (libepoxy); shown when the
  scene cannot be read or has nothing drawable.
- `Screenshot` headless rendering into an RGBA8 framebuffer object and an
  atomic PNG write through libpng (`--screenshot`, `--size`, `--frames`,
  `--device`); `writePng()` also takes 1 (gray) and 3 (RGB) channel images
  for AssetTools.
- `Sandbox` Landlock self restriction applied after the display connection
  and GL context exist: no file writes outside the cache, runtime and temp
  directories (device nodes stay writable for the GPU driver), no TCP bind
  or connect, PR_SET_NO_NEW_PRIVS. Reads stay open. Logs and continues
  unsandboxed on kernels without Landlock; `--no-sandbox` turns it off.

## Pacing contract

A tick advances the renderer once (dt follows the wall clock, capped at
0.1 s), then renders and presents on every output that is ready, not
covered and not waiting for its previous frame callback. The clock runs only
while unpaused and at least one output is uncovered.

## Libraries

`canvas_scene_host` (SceneHost, FrameClock; links canvas_core) is shared
with the tests; the executable adds the GL parts and the asset tools and
links canvas_scene_core, canvas_display, libepoxy and libpng. Built when
`DESKTOP_CANVAS_SCENE` is on (default) and those are found
(canvas_scene_core needs lz4 and libjpeg-turbo too).
