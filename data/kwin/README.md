# data/kwin/

`desktop-canvas-bridge.js.in` the KWin script behind the KDE bridge
(`src/desktop/KdeBridge.h`). CMake fills in the bus name, object path and
interface from `CANVAS_APP_ID` and writes `build/kwin/desktop-canvas-bridge.js`,
which a development build uses from next to the daemon binary; installs put
it in `share/desktop-canvas/kwin/`.

The daemon loads it into the running KWin over D-Bus while it runs and
unloads it on exit; it is never registered as a permanent KWin script. It
uses the Plasma 6 scripting API (`workspace.cursorPos`, `cursorPosChanged`,
`windowList()`, `windowAdded/Removed/Activated`, window `fullScreen`,
`output.name`, `desktopFileName`, `resourceClass`, `normalWindow`) and
`callDBus`. Scripts have no timers, so cursor updates are throttled by
timestamp (at most one per 8 ms); arguments are strings so JavaScript number
types cannot break the D-Bus signatures.
