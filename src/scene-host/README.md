# src/scene-host/

`desktop-canvas-scene`, the native scene renderer helper. The daemon starts
it for scene wallpapers on the "native" and "auto" engines (see
`render/InstanceManager.h`), supervised like the web helper, and drives it
over the control channel protocol in `process/HelperProtocol.h`. It also
runs by hand for previews, screenshots and checks.

Status: plan phase P0b. The scene engine itself (`src/scene/`, phases P1
onward) does not exist yet, so the support check reports everything as
unsupported and "native" mode shows a solid dark slate placeholder. Every
other part (surfaces, pacing, protocol, sandbox, headless screenshots) is
real and tested.

Why a separate process: workshop content is untrusted (binary formats,
shaders, JavaScript); a crash or hang takes down only the helper, whose
memory is fully returned on exit, and loading or shader compilation never
blocks the daemon's single threaded event loop.

## Files

- `main.cpp` command line, mode dispatch, platform selection (Wayland, then
  X11) with a GL 3.3 core, robust, alpha-less EGL context, sandbox, then
  SceneHost. Modes and flags are listed in its header comment; exit codes
  follow HelperProtocol (0 clean, 2 usage, 3 unsupported, 1 failure).
- `SceneHost` everything around the pixels: one RenderSurface per wanted
  output (dropped when the output disappears, recreated when it returns or
  reports `closed()`), frame pacing, and the helper side of the protocol
  (hello, ready once every output presented, stats every 2 s as heartbeat;
  pause, resume, quit, mute, volume, pointer, props, covered). Pointer
  motion and buttons the helper sees itself (its own Wayland surfaces on
  wlroots, polling on X11) go to the renderer too; on Plasma the daemon
  forwards the KDE bridge's cursor as "pointer" messages. `--span`
  (`SceneHostOptions::span`) is accepted and logged; each output renders
  the whole frame until the engine composes scenes (phase P4). No GL, so
  `claude_tests/test_scene_host` drives it with fakes.
- `FrameClock` fixed rate ticker on absolute deadlines: no drift, and no
  catch up burst after a stall (it restarts from now).
- `Renderer` the interface SceneHost drives: `advance(dt)` once per tick,
  then `render(width, height)` per output into the bound framebuffer, plus
  live pointer, property, mute and volume inputs. The scene engine will
  implement it.
- `PlaceholderRenderer` clears to one colour (libepoxy).
- `Screenshot` headless rendering into an RGBA8 framebuffer object and an
  atomic PNG write through libpng (`--screenshot`, `--size`, `--frames`,
  `--device`).
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
with the tests; the executable adds the GL parts and links canvas_display,
libepoxy and libpng. Built when `DESKTOP_CANVAS_SCENE` is on (default) and
those are found.
