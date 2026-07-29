# src/input/

Pointer forwarding to wallpapers that support mouse interaction.

- `X11PointerPoller` polls XQueryPointer on the root window (default 30 Hz),
  maps global coordinates to the containing output, and reports output
  local positions. Works regardless of stacking/occlusion.
- Wayland pointer events are handled inside
  display/wayland/WaylandConnection (surface enter/motion on our layer
  surfaces); they only arrive where no other surface covers the wallpaper.

Both paths deliver through Platform::setOnPointer to InstanceManager, which
routes to the instance on that output (WallpaperInstance::setPointer).
Scene wallpapers are not routed here: linux-wallpaperengine reads input on
its own surface (disable with config scene.disable_mouse).
