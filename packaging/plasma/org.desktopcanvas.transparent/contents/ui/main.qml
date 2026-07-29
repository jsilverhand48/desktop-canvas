// Transparent Plasma wallpaper. Paints nothing and clears the desktop
// containment window's fill color to fully transparent, so an external
// wallpaper surface (the desktop-canvas daemon) shows through while Folder
// View icons and desktop widgets render above it.
//
// The window color reset is required: plasmashell's desktop view clears to
// an opaque color, which would otherwise black out everything behind the
// containment even when the wallpaper itself paints nothing.
import QtQuick
import QtQuick.Window
import org.kde.plasma.plasmoid

WallpaperItem {
    id: root
    anchors.fill: parent

    function makeWindowTransparent() {
        if (root.Window.window)
            root.Window.window.color = Qt.rgba(0, 0, 0, 0);
    }

    Component.onCompleted: makeWindowTransparent()
    Window.onWindowChanged: makeWindowTransparent()
}
