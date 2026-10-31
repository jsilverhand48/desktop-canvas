# src/display/

Platform and surface abstractions.

- `Platform` enumerates outputs and creates `RenderSurface`s; implemented by
  `wayland/WaylandPlatform` and (M4) `x11/X11Platform`. `createWindow`
  makes an ordinary resizable window named "window" instead (xdg-shell or
  a normal X11 window) for `desktop-canvas-scene --window`; its close
  callback runs when the user closes it.
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

Beyond outputs and surfaces a Platform reports, where it can:
- output positions in the global logical layout (Wayland through
  xdg-output, X11 through XRandR), used for spans and for mapping the KDE
  bridge's global cursor to an output;
- pointer motion and buttons over our own surfaces (`setOnPointer`,
  `setOnPointerButton`; buttons on Wayland only);
- other clients' toplevel windows (`watchToplevels`): EWMH on X11
  (`x11/X11Toplevels`), wlr-foreign-toplevel on wlroots compositors
  (`wayland/WaylandToplevels`); false on KWin Wayland, where the KDE bridge
  (`src/desktop/`) fills in.
`OutputGeometry.h` holds the pure helpers over the layout (`outputAt`,
`outputsCoveredBy` with its "at least half of the output" rule).

Build: the backends here, `src/gl/` and `src/input/X11PointerPoller` form
the `canvas_display` static library. Each backend needs only EGL plus its
own libraries (wayland-client and wayland-egl, or x11 and xrandr); libmpv is
not required, since only the daemon's video backend uses it. Linking
`canvas_display` exports `CANVAS_HAVE_WAYLAND` / `CANVAS_HAVE_X11` to the
linking target. The generated Wayland protocol headers stay private to the
library; public headers here only forward declare protocol structs.

`wayland/WaylandLayerOutput` uses the layer shell `background` layer by
default. Do not switch to `bottom` looking for something lower: KWin maps
`bottom` to BelowLayer, which is above the DesktopLayer that `background`
maps to (and that plasmashell's desktop containment lives in). Plasma icon
stacking is handled by packaging/plasma/, not by the layer choice.
