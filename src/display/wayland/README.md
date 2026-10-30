# src/display/wayland/

Wayland implementation of the display abstractions.

- `WaylandConnection` display connect, registry binds (wl_compositor,
  zwlr_layer_shell_v1, zxdg_output_manager_v1, wl_output, wl_seat), output
  tracking with connector names and logical position and size, pointer
  motion and buttons over registered surfaces, event loop integration
  (prepare_read pattern). A zwlr_foreign_toplevel_manager_v1 global is only
  remembered.
- `WaylandToplevels` binds that manager on demand (`watchToplevels`) and
  reports other clients' windows (app id, outputs, fullscreen, activated),
  published on each handle's `done` so changes arrive atomically.
- `WaylandLayerOutput` RenderSurface on a background layer surface:
  anchored to all edges, exclusive zone -1, keyboard interactivity none,
  integer buffer scale, frame callback based pacing.
- `WaylandPlatform` ties both together and owns the shared EglContext,
  created with the `EglContext::Options` passed to its constructor (the
  daemon uses the defaults, desktop-canvas-scene a GL 3.3 core context
  without alpha). `egl()` exposes the context.

Requires a compositor with wlr-layer-shell (KWin, wlroots family). Protocol
headers are generated at build time from XML vendored in protocol/ (the
layer shell "namespace" argument is renamed to "scope" there because
namespace is a C++ keyword; wire format is unaffected).
