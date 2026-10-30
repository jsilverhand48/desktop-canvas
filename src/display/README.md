# src/display/

Platform and surface abstractions.

- `Platform` enumerates outputs and creates `RenderSurface`s; implemented by
  `wayland/WaylandPlatform` and (M4) `x11/X11Platform`.
- `RenderSurface` a GPU surface an in-process backend renders into, with a
  frame pacing contract: swap, then requestFrame(cb), and do not swap again
  until cb fires. This prevents blocking on occluded surfaces.
  Implementations share one EGL context, so their destructors must call
  `EglContext::releaseCurrent()` before `destroySurface()`; see `src/gl/`.
  Surfaces are reused across wallpapers while `closed()` is false.
  `show()`/`hide()` map and unmap without destroying, and `hide()` also
  drops pending frame and ready callbacks. X11 windows are reused
  indefinitely. A Wayland layer surface reports `closed()` after its first
  `hide()` (or when the compositor closes it), and the owner replaces it:
  KWin closes a layer surface that is remapped after a null buffer unmap,
  which left the output black after switching back to a video wallpaper.

Scene wallpapers bypass RenderSurface entirely; their child process creates
its own surface (see src/process/).

`wayland/WaylandLayerOutput` uses the layer shell `background` layer by
default. Do not switch to `bottom` looking for something lower: KWin maps
`bottom` to BelowLayer, which is above the DesktopLayer that `background`
maps to (and that plasmashell's desktop containment lives in). Plasma icon
stacking is handled by packaging/plasma/, not by the layer choice.
