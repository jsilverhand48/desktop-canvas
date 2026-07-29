# src/display/

Platform and surface abstractions.

- `Platform` enumerates outputs and creates `RenderSurface`s; implemented by
  `wayland/WaylandPlatform` and (M4) `x11/X11Platform`.
- `RenderSurface` a GPU surface an in-process backend renders into, with a
  frame pacing contract: swap, then requestFrame(cb), and do not swap again
  until cb fires. This prevents blocking on occluded surfaces.

Scene wallpapers bypass RenderSurface entirely; their child process creates
its own surface (see src/process/).
