# src/

Daemon source tree. Module map:

- `main.cpp` CLI parsing, platform selection, daemon wiring, IPC dispatch
- `core/` event loop (epoll), logging
- `config/` config file schema, load/save, CLI merge
- `source/` workshop content scanner (project.json parsing), playlist file
  reader for rotation
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

Data flow: main.cpp builds Config, picks a Platform, constructs
InstanceManager, which scans wallpapers (source/), reconciles config
assignments with outputs, and creates per output instances (render/ or
process/). IPC commands mutate config through InstanceManager and are
persisted immediately.

When a playlist and a positive interval are configured, main.cpp also starts
a Rotator after the first apply(). It draws a random id from the playlist
every interval and pushes it into InstanceManager as a runtime override, so
rotation reuses the same reconciliation path as an IPC set without ever
rewriting the config assignments.
