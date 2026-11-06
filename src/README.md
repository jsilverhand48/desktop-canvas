# src/

Daemon source tree. Module map:

- `main.cpp` CLI parsing, platform selection, daemon wiring (Controller,
  IPC socket, settings UI server, library auto refresh via
  source/LibraryWatcher), one time save after a legacy ids file import.
  runDaemon() caps glibc at two malloc arenas (unless MALLOC_ARENA_MAX is
  set) before any thread starts, so memory freed by libmpv's many threads
  is actually returned to the system; see render/README.md (VideoBackend).
  When built with libmpv (CANVAS_HAVE_VIDEO) it installs VideoInstance as
  InstanceManager's VideoFactory. It feeds the platform's toplevel windows
  to InstanceManager for the fullscreen pause and, on Plasma Wayland with
  `kde_bridge` on, starts the KDE bridge (`--probe-kde-bridge N` runs only
  the bridge, for diagnosis). X11 surfaces pace at `config.fps`.
- `core/` event loop (epoll), logging
- `config/` config file schema, load/save, CLI merge
- `source/` workshop content scanner (project.json parsing), the rotation
  pool list (and legacy ids file import), scene user properties and random
  variant picks
- `display/` platform and surface abstractions; `wayland/` layer shell
  implementation; `x11/` DESKTOP window implementation (M4)
- `gl/` EGL display/context/surface helpers
- `render/` WallpaperInstance interface, InstanceManager orchestration,
  Rotator (timed playlist rotation), VideoBackend (libmpv, compiled into
  the executable only)
- `web-helper/` desktop-canvas-web, the optional Qt6 WebEngine child
  process for web wallpapers (DESKTOP_CANVAS_WEB=ON)
- `scene/` the native scene engine (clean room rewrite, see its README):
  so far `assets/` (scene.pkg, file lookup, .tex readers; plan phase P1)
  and `model/` (scene description, user properties, capability report;
  P2), `shader/` (dialect translation to GLSL 3.30; P3), `render/`
  (layout and timeline animation math; P4) and `gl/` (program cache,
  textures, video textures and the scene renderer: layers with effects,
  bloom, animation, camera shake and parallax; P3 and P4)
- `scene-host/` desktop-canvas-scene, the native scene renderer helper
  (engines "native" and "auto"), its `--extract`/`--dump` asset tools and
  `--render-debug` diagnostics, DESKTOP_CANVAS_SCENE=ON
- `process/` child process supervision (process groups, death signal,
  control channel on fd 3, exit policies), the daemon/helper protocol,
  helper lookup, linux-wallpaperengine command construction, and the
  instance types for lwe/web children and the native helper
- `input/` pointer forwarding (X11 poller lands with M4/M5)
- `desktop/` desktop integration: the fullscreen pause policy and the KDE
  bridge (sd-bus service plus a KWin script loaded at runtime, for the
  cursor under the Plasma desktop icons and fullscreen windows)
- `audio/` mute/volume policy (muted by default)
- `ipc/` unix socket control protocol shared with tools/canvasctl
- `control/` Controller: every action and setting, shared by IPC and the
  settings UI
- `http/` 127.0.0.1 HTTP server and JSON routes for the settings UI
  (front end files in data/ui)

Build layout (root CMakeLists.txt): the tree compiles into static libraries
shared by the daemon, helper binaries and tests. `canvas_core` is `core/`;
`canvas_display` is `gl/`, `display/` and `input/X11PointerPoller` (built when
EGL plus Wayland or X11 are available, never needs libmpv); `canvas_daemon`
is `config/`, `source/`, `render/` except VideoBackend, `process/`, `audio/`,
`ipc/`, `control/` and `http/`, and links no display or video library
(libpulse when found, for `audio/PulseCapture`). The
`desktop-canvas` executable is `main.cpp` plus those, with
`render/VideoBackend.cpp` and libmpv added when mpv is found.
`canvas_scene_props` is `scene/model/UserProperties` (no dependencies;
linked by `canvas_daemon` for SceneOptions and by the engine).
`canvas_scene_core` is the rest of `scene/` except `gl/` (no GPU code;
links lz4, libjpeg-turbo, libpng, FreeType and, when found, fontconfig
and QuickJS-ng);
`canvas_scene_gl` is `scene/gl/`
(libepoxy). `canvas_scene_host` (scene-host pacing and protocol, no GL) plus
the GL parts and asset tools, canvas_scene_core, canvas_display, libepoxy
and libpng make `desktop-canvas-scene`.

Data flow: main.cpp builds Config, picks a Platform, constructs
InstanceManager, which scans wallpapers (source/), reconciles config
assignments with outputs, and creates per output instances (render/ or
process/). IPC commands and settings UI requests both go through Controller,
which mutates config, persists it immediately, and triggers the needed live
effect (restart instances, refresh rotation, ...).

When some wallpapers are enabled and the interval is positive, main.cpp also
starts a Rotator after the first apply(). It draws a random enabled id every
interval and pushes it into InstanceManager as a runtime override, so
rotation reuses the same reconciliation path as an IPC set without ever
rewriting the config assignments.
