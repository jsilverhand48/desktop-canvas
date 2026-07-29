# desktop-canvas

Animated and interactive wallpapers for Linux desktops, rendering Wallpaper
Engine workshop content behind your icons, widgets, and taskbars.

Supported wallpaper types:

| Type | How it renders | Notes |
|---|---|---|
| scene (scene.pkg) | supervised linux-wallpaperengine child process | needs linux-wallpaperengine installed (or bundled) |
| video (mp4 etc.) | in process via libmpv into a layer shell surface | hardware decoded when available |
| web (HTML/JS) | supervised Qt6 WebEngine (Quick) child process | build with -DDESKTOP_CANVAS_WEB=ON; audio reactive visualizers render their static state (no spectrum feed yet) |
| presets | not supported | items whose project.json has a "dependency" key |

Audio from wallpapers is muted by default; unmute with
`desktop-canvasctl unmute` or `--unmute`.

## Requirements

- Wayland compositor implementing wlr-layer-shell (KDE Plasma, Hyprland,
  Sway, and other wlroots compositors), or X11 (backend in progress).
  GNOME Wayland is not supported (Mutter lacks layer-shell).
- libmpv, wayland, EGL at runtime; linux-wallpaperengine for scene
  wallpapers.
- Wallpaper Engine workshop content, e.g. a Steam library containing
  `steamapps/workshop/content/431960`. Content roots are autodetected from
  `~/.local/share/Steam`, `~/.steam/steam`, and
  `/run/media/$USER/*/SteamLibrary`, or set explicitly with `--root` or the
  config file.

## Build

```
cmake -B build
cmake --build build
cmake --install build          # optional, installs to /usr/local
```

Options: `-DDESKTOP_CANVAS_WEB=ON` (web wallpapers via Qt6 WebEngine),
`-DDESKTOP_CANVAS_BUNDLE_LWE=ON` (download and bundle a pinned
linux-wallpaperengine), `-DDESKTOP_CANVAS_TESTS=OFF`.

## Use

```
desktop-canvas --list                 # scan the library, print id/type/title
desktop-canvas                        # run the daemon (all outputs, config)
desktop-canvas --set '*=1049326028'   # everything shows this wallpaper
desktop-canvas --set 'DP-1=1093126365' --set 'HDMI-A-1=1049326028'

desktop-canvasctl status              # what runs where
desktop-canvasctl set DP-1 1093126365 # change one output, persisted
desktop-canvasctl set '*' none        # clear the wildcard assignment
desktop-canvasctl unmute | mute | pause | resume | reload | quit
```

Config lives at `~/.config/desktop-canvas/config.json` (schema documented in
`src/config/Config.h`). Scene wallpapers that crash repeatedly are added to
the `broken` list there and skipped; remove the id to retry.

### Rotating through a list of wallpapers

Keep the ids you like in a text file, one per line (`#` starts a comment):

```
# ~/.config/wallpaper-engine/ids.txt
2895407554
1767354809
2404179953
```

Point the daemon at it and give it an interval in seconds:

```
desktop-canvas --playlist ~/.config/wallpaper-engine/ids.txt --rotate 900
```

Every 900 seconds a random id from the file is picked. **All screens show the
same wallpaper** unless you name the outputs to rotate:

```
desktop-canvas --playlist ~/.config/wallpaper-engine/ids.txt --rotate 900 \
    --rotate-output DP-5 --rotate-output DP-6   # only these two rotate
```

The same controls are available at runtime, and persist to the config:

```
desktop-canvasctl playlist ~/.config/wallpaper-engine/ids.txt
desktop-canvasctl rotate 15m          # seconds, or 30s / 15m / 2h
desktop-canvasctl rotate 15m DP-5     # limit rotation to one output
desktop-canvasctl rotate off          # stop, keep the playlist
desktop-canvasctl next                # skip to the next wallpaper now
desktop-canvasctl playlist none       # forget the playlist entirely
desktop-canvasctl status              # shows interval and seconds remaining
```

Ids in the file that are not in your library, are an unsupported type, or are
on the `broken` list are skipped when picking, so a stale entry costs one
candidate rather than a blank screen. The wallpaper a rotation picks is not
written to `config.json`: your `assignments` stay untouched, and turning
rotation off restores them. A manual `set` during rotation shows immediately
and holds until the next tick reclaims that output.

Autostart: `systemctl --user enable --now desktop-canvas` after install.

## Desktop environment caveat matrix

The wallpaper is drawn on the layer shell background layer (Wayland) or a
DESKTOP type window (X11). Panels and taskbars are dock windows and always
stay above it. Desktop icons are the tricky part because some DEs draw them
inside their own opaque desktop window:

| Environment | Result |
|---|---|
| KDE Plasma Wayland | Panels stay above. Desktop icons live inside plasmashell's containment window, which by default is covered by the wallpaper (or covers it, racily, after plasmashell restarts). Run `packaging/plasma/setup-plasma-icons.sh` for the deterministic fix: it installs a transparent Plasma wallpaper plugin and a KWin rule pinning the containment above the wallpaper, so icons, widgets, and the desktop context menu all work on top of the animated wallpaper. Tradeoff: the containment then receives pointer input, so scene mouse parallax does not react. |
| Hyprland, Sway, wlroots | Clean: bars (waybar etc.) above, wallpaper behind everything, mouse input reaches the wallpaper over empty desktop. |
| XFCE, MATE, Cinnamon (X11) | Disable the DE's own desktop icon drawing (xfdesktop / caja / nemo desktop settings) so its opaque DESKTOP window does not cover the wallpaper. |
| GNOME Wayland | Not supported. |

Mouse input: on Wayland, pointer events reach the wallpaper only where no
other surface is above it (works on wlroots compositors; on Plasma the
containment usually intercepts). Scene wallpapers manage their own input via
linux-wallpaperengine. `--disable-mouse` behavior is available through the
config (`scene.disable_mouse`).

## Repository layout

- `src/` daemon sources (see `src/README.md` for the module map)
- `tools/canvasctl/` control CLI
- `protocol/` vendored Wayland protocol XML (generated at build time)
- `third_party/` vendored single header libraries (nlohmann json, CLI11)
- `packaging/` systemd unit, autostart entry, Flatpak/PKGBUILD/debian
- `claude_tests/` tests (ctest)
