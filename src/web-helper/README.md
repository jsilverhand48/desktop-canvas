# src/web-helper/

desktop-canvas-web: standalone Qt WebEngine process rendering one web
wallpaper, spawned per output and supervised by the daemon like a scene
child (crash respawn, SIGSTOP pause, mute via restart).

Architecture: the top level is a QQuickView hosting a QML WebEngineView.
QQuickView is used deliberately instead of the widget based QWebEngineView:
LayerShellQt configuration must be attached to the QWindow BEFORE its
platform window exists, and QWebEngineView creates its platform window
eagerly, which silently leaves the surface on LayerShellQt's default layer
(top, above all windows). QQuickView is a QWindow, so the layer settings
(background layer, all edge anchors, exclusive zone -1, keyboard off,
scope "desktop-canvas", target screen) are applied before show().

The QML document is compiled into the binary and written to a QTemporaryFile
under QDir::tempPath() at startup, because QQuickView::setSource takes a URL.
The temp path is explicit: a bare relative QTemporaryFile template resolves
against the current working directory, so the helper used to leave stray
desktop-canvas-web-XXXXXX.qml files wherever the daemon was launched from.
The file auto-removes on exit; a hard kill of the helper still leaves one.

Placement: layer shell background surface on Wayland (--output NAME); on
X11 a frameless window at --geometry that the daemon demotes to the desktop
layer by pid. Audio muted unless --unmuted.

Wallpaper Engine integration: a script injected at document creation
provides wallpaperRegisterAudioListener / wallpaperRequestRandomFileForProperty
stubs and calls applyUserProperties with the wallpaper's default properties
parsed from --project project.json (shape: { name: {"value": ...} }).
Chromium runs with --allow-file-access-from-files and autoplay enabled so
file:// wallpapers load their assets and media. The context menu is
suppressed (a wallpaper must never spawn popups over the desktop).

Limitation: no audio spectrum data is fed to wallpapers yet, so purely
audio reactive wallpapers (visualizers) render their static state.

Built only with -DDESKTOP_CANVAS_WEB=ON (Qt6 Gui + Quick + WebEngineQuick;
LayerShellQt required for Wayland, otherwise the helper refuses to start on
Wayland rather than covering the screen with an unlayered window).
