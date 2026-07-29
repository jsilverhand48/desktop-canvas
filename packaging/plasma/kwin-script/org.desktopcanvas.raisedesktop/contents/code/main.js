// Keeps plasmashell's desktop containment (the window that draws desktop
// icons and widgets) stacked above external wallpaper surfaces.
//
// Why a script and not a window rule:
//
//   KWin's Window::belongsToLayer() tests isDesktop() before keepAbove():
//
//       if (isDesktop()) return showingDesktop() ? AboveLayer : DesktopLayer;
//       ...
//       if (keepAbove()) return AboveLayer;
//
//   so a "keep above" window rule can never promote a desktop-type window out
//   of DesktopLayer. LayerShellV1Window::belongsToLayer() maps a wlr-layer-shell
//   BackgroundLayer surface to that same DesktopLayer, and `bottom` is no help
//   because it maps to BelowLayer, which sits *above* DesktopLayer. So the
//   wallpaper and the containment always share one layer, ordered purely by map
//   order: whichever surface mapped last wins. Restarting the wallpaper daemon
//   (or any wallpaper reassignment that recreates a surface) buries the icons.
//
// Raising the containment moves it to the top of its layer without changing the
// layer itself, so normal windows are unaffected. Doing it on every windowAdded
// keeps the ordering correct no matter which process maps a wallpaper surface,
// including wallpaper tools desktop-canvas does not manage.

function raiseDesktops() {
    const windows = workspace.windowList();
    for (let i = 0; i < windows.length; ++i) {
        if (windows[i].desktopWindow) {
            workspace.raiseWindow(windows[i]);
        }
    }
}

workspace.windowAdded.connect(raiseDesktops);

// Apply immediately on load, so enabling the script fixes an already-buried
// desktop without a plasmashell restart.
raiseDesktops();
