# packaging/plasma/

KDE Plasma integration so desktop icons and widgets stay visible above
desktop-canvas wallpapers.

Background: on Plasma, icons live inside plasmashell's desktop containment
window together with the Plasma wallpaper. KWin stacks wallpaper layer
surfaces and the containment within the same desktop layer in map order,
so an external wallpaper normally covers the icons (or, after a plasmashell
restart, the opaque Plasma wallpaper covers the external one). Two pieces
fix this deterministically:

- `org.desktopcanvas.transparent/` a Plasma wallpaper plugin that paints
  nothing and clears the desktop window's opaque fill color, making the
  containment fully transparent.
- A KWin window rule (Description "desktop-canvas: keep desktop containment
  above wallpaper layer") forcing keep above on plasmashell desktop
  windows, which pins the containment above wallpaper surfaces regardless
  of map order.

`setup-plasma-icons.sh` applies both and restarts plasmashell. Revert
instructions are in the script header.

Tradeoff: with the containment above the wallpaper, pointer input goes to
the desktop (icons, right click menu) and not to the wallpaper, so scene
mouse parallax does not react on Plasma. This is inherent: whichever
surface is on top receives the input.
