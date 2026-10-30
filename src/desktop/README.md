# src/desktop/

Desktop integration that is not a display backend: what other windows are
doing, and Plasma specifics. Part of the `canvas_daemon` library.

- `FullscreenPolicy` `coveredOutputs(windows, rules)`: the outputs a
  fullscreen application hides, from the toplevel list a platform or the
  KDE bridge reports and the config rules (`fullscreen_pause`: enabled,
  only while focused, app id substrings to ignore, case insensitive). Pure
  function; InstanceManager applies the result (pauses instances whose
  outputs are all covered and tells every instance which outputs are).
- `KdeBridge` (built when libsystemd is found, `CANVAS_HAVE_SDBUS`): a
  session bus service named after the app id
  (`io.github.jsilverhand48.DesktopCanvas`, object
  `/io/github/jsilverhand48/DesktopCanvas`, interface `...DesktopCanvas.Bridge`
  with `Cursor(s)` and `Windows(s)`). On Plasma Wayland, with config
  `kde_bridge` on, main.cpp starts it and it loads the KWin script
  generated from `data/kwin/desktop-canvas-bridge.js.in` through
  `org.kde.KWin /Scripting` (unloading a stale copy first), runs it, loads
  it again when KWin restarts, and unloads it on exit. The script reports
  the cursor in global logical coordinates (KWin knows it even while the
  Plasma desktop surface takes the pointer) and the fullscreen windows. Only
  calls from KWin's own bus connection are accepted. The cursor reaches the
  native scene helper through `InstanceManager::pointerAtGlobal`;
  linux-wallpaperengine reads its own input, so lwe scenes do not get it.
  `desktop-canvas --probe-kde-bridge N` runs only the bridge for N seconds
  and prints what KWin reports.

Where windows come from: X11 (EWMH, `display/x11/X11Toplevels`) and wlroots
compositors (`display/wayland/WaylandToplevels`) report them through
`Platform::watchToplevels`; KWin offers neither to Wayland clients, so on
Plasma Wayland the bridge is the only source.
