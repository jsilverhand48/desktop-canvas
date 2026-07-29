# packaging/plasma/

KDE Plasma integration so desktop icons and widgets stay visible above
desktop-canvas wallpapers.

## Background

On Plasma, icons live inside plasmashell's desktop containment window
together with the Plasma wallpaper. KWin puts that containment and every
wlr-layer-shell `background` surface into the same layer:

- `LayerShellV1Window::belongsToLayer()` maps `BackgroundLayer` to
  `DesktopLayer`.
- `Window::belongsToLayer()` returns `DesktopLayer` for desktop-type
  windows.

Within a layer, stacking is plain map order: whichever surface mapped last
is on top. So an external wallpaper covers the icons after the wallpaper
daemon restarts, and the Plasma wallpaper covers the external one after a
plasmashell restart.

The `bottom` layer is not an escape hatch. It maps to `BelowLayer`, which
sits *above* `DesktopLayer` in KWin's layer order, so it would put the
wallpaper even more firmly on top. `background` is already the lowest
reachable layer.

A "keep above" window rule does not work either, and earlier versions of
this directory shipped one that never did anything. `belongsToLayer()`
short-circuits on the window type before it consults `keepAbove()`:

```cpp
if (isDesktop()) return showingDesktop() ? AboveLayer : DesktopLayer;
...
if (keepAbove()) return AboveLayer;
```

A desktop-type window can therefore never be promoted out of `DesktopLayer`
by a rule. The old rule only appeared to work because `setup-plasma-icons.sh`
restarted plasmashell as its last step, which happened to remap the
containment after the wallpaper surfaces.

## The fix

Two pieces:

- `org.desktopcanvas.transparent/` a Plasma wallpaper plugin that paints
  nothing and clears the desktop window's opaque fill color, making the
  containment fully transparent. Without the fill-color reset the
  containment would black out everything behind it even when the wallpaper
  paints nothing.
- `kwin-script/org.desktopcanvas.raisedesktop/` a KWin script that raises
  every desktop containment on `workspace.windowAdded`, and once at load.
  Raising moves the containment to the top of `DesktopLayer` without
  changing its layer, so normal windows are unaffected. Because it reacts
  to any window mapping, it also covers wallpaper tools that desktop-canvas
  does not manage.

`setup-plasma-icons.sh` installs and enables both, and removes the obsolete
window rule. It needs no plasmashell or session restart. Revert
instructions are in the script header.

## Tradeoff

With the containment above the wallpaper, pointer input goes to the desktop
(icons, right click menu) and not to the wallpaper, so scene mouse parallax
does not react on Plasma. This is inherent: whichever surface is on top
receives the input.

## Debugging

```sh
# is the script loaded?
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.isScriptLoaded \
    org.desktopcanvas.raisedesktop

# script errors and console output land in the journal
journalctl --user -b -o cat | grep -i 'js:'
```
