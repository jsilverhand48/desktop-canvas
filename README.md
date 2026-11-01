# desktop-canvas

Animated and interactive wallpapers for Linux desktops, rendering Wallpaper
Engine workshop content behind your icons, widgets, and taskbars.

Supported wallpaper types:

| Type | How it renders | Notes |
|---|---|---|
| scene (scene.pkg) | supervised desktop-canvas-scene (the native renderer, in development) or linux-wallpaperengine child process | `scene.engine`: `auto` (default: native where it can, linux-wallpaperengine for the rest), `native` or `lwe`; the native renderer draws image, effect and particle scenes (116 of the 208 scenes of the test library), not yet scripts, text, sound, puppets or lights, so other scenes still need linux-wallpaperengine installed (or bundled) |
| video (mp4 etc.) | in process via libmpv into a layer shell surface | hardware decoded when available |
| web (HTML/JS) | supervised Qt6 WebEngine (Quick) child process | build with -DDESKTOP_CANVAS_WEB=ON; audio reactive visualizers render their static state (no spectrum feed yet) |
| presets | not supported | items whose project.json has a "dependency" key |

Audio from wallpapers is muted by default; unmute with
`desktop-canvasctl unmute` or `--unmute`.

## Requirements

- Wayland compositor implementing wlr-layer-shell (KDE Plasma, Hyprland,
  Sway, and other wlroots compositors), or X11 (backend in progress).
  GNOME Wayland is not supported (Mutter lacks layer-shell).
- wayland (or X11) and EGL at runtime; libmpv for video wallpapers;
  libepoxy, libpng, lz4, libjpeg-turbo and FreeType (fontconfig for
  system font names) for desktop-canvas-scene; libsystemd (sd-bus) for
  the KDE bridge; linux-wallpaperengine for scene wallpapers on the `lwe`
  engine and for what desktop-canvas-scene does not draw yet.
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
linux-wallpaperengine), `-DDESKTOP_CANVAS_TESTS=OFF`. Tests build by default
when `claude_tests/` is present and are skipped when it is not. libmpv is
optional at build time: without its development files configure warns and
the daemon is built without video support, while scene and web wallpapers
still work. `-DDESKTOP_CANVAS_SCENE=OFF` skips desktop-canvas-scene (built
by default when the libepoxy, libpng, lz4 and libjpeg-turbo development
files are present). `-DDESKTOP_CANVAS_FUZZ=ON` (clang only, separate build
directory) builds the sanitized libFuzzer targets in `claude_tests/fuzz/`.

desktop-canvas-scene can also inspect a scene's files the way its renderer
finds them: `desktop-canvas-scene --extract materials/x.tex x.png DIR`
decodes one texture (or copies any other file),
`desktop-canvas-scene --dump OUT DIR` unpacks the item's scene.pkg,
`desktop-canvas-scene --check DIR` prints which features the scene uses
and which this build cannot render yet, and
`desktop-canvas-scene --check-all ROOT [--json]` does that for a whole
workshop folder.

Installing places the `desktop-canvas` daemon and `desktop-canvasctl` in
`bin`, plus a systemd user unit, an XDG desktop entry, AppStream metainfo, an
application icon, and the KDE Plasma wallpaper plugin and KWin script. The
Plasma pieces are installed but not activated; see the caveat matrix below.

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

### Settings UI

The daemon serves a settings page at **http://127.0.0.1:8765/** (this
computer only; `desktop-canvasctl status` prints the address). From it you
can:

- browse every wallpaper in your Steam library (previews of Mature and
  Questionable items are blurred until clicked) and enable or disable each
  one for rotation, singly or in bulk for the current filter
- per scene: pin any user property (dropdowns, sliders, toggles, colours,
  text) and choose which dropdown options random picks may use
- edit every other setting: FPS, scaling, layer, audio, rotation interval
  and outputs, per output assignments, engine and assets paths, library
  folders, the broken list
- show a wallpaper now, skip to the next one, pause, mute, restart

The library follows the Steam folder on its own: wallpapers you subscribe
to appear (once their download finishes) and removed ones disappear within
a few seconds, and a library drive plugged in later is picked up within
about 30 seconds. No reload needed.

Changes are saved to `config.json` and take effect immediately (port and
UI on/off after a daemon restart). Disable the page with `"ui":
{"enabled": false}`.

### Rotating through wallpapers

The rotation pool is every wallpaper marked **enabled** (in the settings UI,
or `"wallpapers": {"<id>": {"enabled": true}}` in `config.json`). Give
rotation an interval in seconds:

```
desktop-canvas --rotate 900
```

Every 900 seconds a random enabled wallpaper is picked. **All screens show the
same wallpaper** unless you name the outputs to rotate:

```
desktop-canvas --rotate 900 --rotate-output DP-5 --rotate-output DP-6
```

The same controls are available at runtime, and persist to the config:

```
desktop-canvasctl enable 2895407554 1767354809   # add to the pool
desktop-canvasctl disable 2404179953             # remove from the pool
desktop-canvasctl playlist ids.txt    # enable every id listed in a file
desktop-canvasctl playlist none       # disable every wallpaper
desktop-canvasctl rotate 15m          # seconds, or 30s / 15m / 2h
desktop-canvasctl rotate 15m DP-5     # limit rotation to one output
desktop-canvasctl rotate off          # stop, keep the enabled set
desktop-canvasctl next                # skip to the next wallpaper now
desktop-canvasctl status              # interval, seconds remaining, UI address
```

Older versions kept the ids in a separate file (`playlist.file`, typically
`~/.config/wallpaper-engine/ids.txt`). On first start the daemon imports it
into the enabled set and stops referring to it; the file itself is left in
place. `--playlist <file>` imports a file the same way.

Enabled ids that are not in your library, are an unsupported type, or are on
the `broken` list are skipped when picking, so a stale entry costs one
candidate rather than a blank screen. The wallpaper a rotation picks is not
written to `config.json`: your `assignments` stay untouched, and turning
rotation off restores them. A manual `set` during rotation shows immediately
and holds until the next tick reclaims that output.

### Random scene variants

Many scenes offer poses, outfits or layouts as dropdowns. With **Random
dropdown values** on (Settings > Scenes, `scene.randomize_options`), each
launch picks a random value for every dropdown that is not pinned, limited to
the options you ticked for that wallpaper. Options labelled NSFW or nude are
skipped unless you allow them globally (`scene.include_nsfw_options`) or tick
them for a specific wallpaper. With a single wallpaper enabled, each rotation
tick relaunches it so a new variant shows.

Autostart: `systemctl --user enable --now desktop-canvas` after install.

## Desktop environment caveat matrix

The wallpaper is drawn on the layer shell background layer (Wayland) or a
DESKTOP type window (X11). Panels and taskbars are dock windows and always
stay above it. Desktop icons are the tricky part because some DEs draw them
inside their own opaque desktop window:

| Environment | Result |
|---|---|
| KDE Plasma Wayland | Panels stay above. Desktop icons live inside plasmashell's containment window, which KWin puts in the same layer as the wallpaper surface and stacks by map order, so whichever mapped last wins: the wallpaper covers the icons after the daemon restarts, and the Plasma wallpaper covers the wallpaper after plasmashell restarts. Run `packaging/plasma/setup-plasma-icons.sh` for the deterministic fix: it installs a transparent Plasma wallpaper plugin plus a KWin script that re-raises the containment whenever a window maps, so icons, widgets, and the desktop context menu all work on top of the animated wallpaper. Tradeoff: the containment then receives pointer input, so scene mouse parallax does not react. |
| Hyprland, Sway, wlroots | Clean: bars (waybar etc.) above, wallpaper behind everything, mouse input reaches the wallpaper over empty desktop. |
| XFCE, MATE, Cinnamon (X11) | Disable the DE's own desktop icon drawing (xfdesktop / caja / nemo desktop settings) so its opaque DESKTOP window does not cover the wallpaper. |
| GNOME Wayland | Not supported. |

Mouse input: on Wayland, pointer events reach the wallpaper only where no
other surface is above it (works on wlroots compositors; on Plasma the
containment usually intercepts). On Plasma Wayland the KDE bridge (config
`kde_bridge`, on by default; needs libsystemd at build time) loads a small
KWin script while the daemon runs that reports the cursor position, which
the daemon forwards to scenes rendered by desktop-canvas-scene.
linux-wallpaperengine reads its own input, so lwe scenes still do not react
under the Plasma icons. `--disable-mouse` behavior is available through the
config (`scene.disable_mouse`). Check the bridge with
`desktop-canvas --probe-kde-bridge 10` (while no daemon is running): it
prints the cursor and fullscreen windows KWin reports.

Fullscreen apps: wallpapers on a monitor covered by a fullscreen window
pause until it leaves (config `fullscreen_pause`: `enabled`, `only_active`,
`ignore_app_ids`). Windows are seen through EWMH on X11,
wlr-foreign-toplevel-management on wlroots compositors and the KDE bridge
on Plasma Wayland.

Screen spans: config `spans` lists groups of outputs (for example
`[["DP-5", "DP-6"]]`) that show one scene as a single canvas stretched over
them whenever all of them show that scene (`--screen-span` for
linux-wallpaperengine).

Steam libraries are found in native, Flatpak and snap Steam installs and in
every library their `libraryfolders.vdf` lists, plus
`/run/media/$USER/*/SteamLibrary`.

## Repository layout

- `src/` daemon sources (see `src/README.md` for the module map)
- `tools/canvasctl/` control CLI
- `data/ui/` settings UI (HTML/CSS/JS served by the daemon)
- `protocol/` vendored Wayland protocol XML (generated at build time)
- `third_party/` vendored single header libraries (nlohmann json, CLI11)
- `packaging/` desktop entry, AppStream metainfo, icon, systemd unit, Plasma
  integration, and Flatpak/PKGBUILD/debian recipes (see `packaging/README.md`)
- `claude_tests/` tests (ctest), untracked, so absent from release tarballs

## License

MIT, see `LICENSE`. The vendored headers under `third_party/` keep their own
licences (nlohmann json MIT, CLI11 BSD 3 clause) alongside the sources.
