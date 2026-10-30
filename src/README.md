# src/

Daemon source tree. Module map:

- `main.cpp` CLI parsing, platform selection, daemon wiring (Controller,
  IPC socket, settings UI server, library auto refresh via
  source/LibraryWatcher), one time save after a legacy ids file import.
  runDaemon() caps glibc at two malloc arenas (unless MALLOC_ARENA_MAX is
  set) before any thread starts, so memory freed by libmpv's many threads
  is actually returned to the system; see render/README.md (VideoBackend).
- `core/` event loop (epoll), logging
- `config/` config file schema, load/save, CLI merge
- `source/` workshop content scanner (project.json parsing), the rotation
  pool list (and legacy ids file import), scene user properties and random
  variant picks
- `display/` platform and surface abstractions; `wayland/` layer shell
  implementation; `x11/` DESKTOP window implementation (M4)
- `gl/` EGL display/context/surface helpers
- `render/` WallpaperInstance interface, InstanceManager orchestration,
  Rotator (timed playlist rotation), VideoBackend (libmpv)
- `web-helper/` desktop-canvas-web, the optional Qt6 WebEngine child
  process for web wallpapers (DESKTOP_CANVAS_WEB=ON)
- `process/` child process supervision and linux-wallpaperengine command
  construction for scene wallpapers
- `input/` pointer forwarding (X11 poller lands with M4/M5)
- `audio/` mute/volume policy (muted by default)
- `ipc/` unix socket control protocol shared with tools/canvasctl
- `control/` Controller: every action and setting, shared by IPC and the
  settings UI
- `http/` 127.0.0.1 HTTP server and JSON routes for the settings UI
  (front end files in data/ui)

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
