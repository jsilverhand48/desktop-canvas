# src/display/wayland/

Wayland implementation of the display abstractions.

- `WaylandConnection` display connect, registry binds (wl_compositor,
  zwlr_layer_shell_v1, zxdg_output_manager_v1, wl_output), output tracking
  with connector names, event loop integration (prepare_read pattern).
- `WaylandLayerOutput` RenderSurface on a background layer surface:
  anchored to all edges, exclusive zone -1, keyboard interactivity none,
  integer buffer scale, frame callback based pacing.
- `WaylandPlatform` ties both together and owns the shared EglContext.

Requires a compositor with wlr-layer-shell (KWin, wlroots family). Protocol
headers are generated at build time from XML vendored in protocol/ (the
layer shell "namespace" argument is renamed to "scope" there because
namespace is a C++ keyword; wire format is unaffected).
