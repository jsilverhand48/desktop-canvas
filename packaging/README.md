# packaging/

Distribution artifacts.

## Application identity

The reverse DNS application id is `io.github.jsilverhand48.DesktopCanvas`,
set once as `CANVAS_APP_ID` in the top level `CMakeLists.txt`. AppStream
ties the component id to the desktop entry file name, and Flatpak requires
the id to sit under a namespace the project controls, so these three files
share that basename and must be renamed together:

- `desktop/io.github.jsilverhand48.DesktopCanvas.desktop` XDG desktop entry
  (installed to `share/applications`).
- `metainfo/io.github.jsilverhand48.DesktopCanvas.metainfo.xml` AppStream
  metadata (installed to `share/metainfo`). Required by Flathub, and what
  makes the app visible in Discover and GNOME Software. Validate with
  `appstreamcli validate`.
- `icons/io.github.jsilverhand48.DesktopCanvas.svg` application icon
  (installed to `share/icons/hicolor/scalable/apps`).

Moving the project to a GitHub organisation means changing `CANVAS_APP_ID`
and those three file names in one go.

## Runtime integration

- `systemd/desktop-canvas.service.in` user unit template (ExecStart is
  filled with the install prefix at configure time); enable with
  `systemctl --user enable --now desktop-canvas`. This is the autostart
  mechanism; the desktop entry above is a menu launcher, not an autostart
  file. `MemoryMax=4G` caps the service cgroup so a leak gets the daemon
  restarted rather than triggering the system wide OOM killer.
- `plasma/` KDE Plasma integration (transparent wallpaper plugin and KWin
  script) so desktop icons render above wallpapers; see `plasma/README.md`.
  Both are installed system wide (`share/plasma/wallpapers` and
  `share/kwin/scripts`) so packages carry them. Installing does not activate
  them: `plasma/setup-plasma-icons.sh` (or the desktop settings) still has
  to select them, and the script remains the per user path for source
  builds.
- Settings UI: `data/ui/` is installed to `share/desktop-canvas/ui` by the
  same `cmake --install`, so every recipe here carries it. The daemon serves
  it on http://127.0.0.1:8765/ (loopback only, config `ui.port`).

## Distribution recipes

Each needs a tagged release tarball and its checksum before it can be
published.

- `arch/` AUR package, checked against the AUR submission guidelines and
  built successfully with `makepkg`. A split package: `desktop-canvas` plus
  `desktop-canvas-web` for the qt6-webengine backend. Carries its own 0BSD
  package sources licence and `REUSE.toml` as RFC40 requires. Only the
  `sha256sums` entry is outstanding, pending the release tarball. See
  `arch/README.md` for the submission workflow.
- `debian/` control and rules stubs. Incomplete: `dh` will not run without
  `debian/changelog`, and `debian/copyright`, `debian/source/format` and
  `debian/watch` are also missing. On Debian and Ubuntu `wayland-scanner`
  ships in `libwayland-bin`, not `libwayland-dev`.
- `flatpak/io.github.desktopcanvas.DesktopCanvas.yml` reference Flatpak
  manifest (KDE runtime for Qt6 WebEngine; bundles linux-wallpaperengine as
  a module). Not submittable as is: the file name and app id predate the id
  above, `sources: type: dir` must become a tagged archive or a pinned git
  source, the metainfo needs screenshots added, and `--socket=session-bus`
  should be narrowed to specific `--talk-name=` entries.

## Notes for packagers

Scene wallpapers depend on linux-wallpaperengine at runtime; distro packages
should Recommend/Suggest it. The native replacement, desktop-canvas-scene,
is built from this repository (libepoxy and libpng at build and run time,
listed in the Arch `depends` and Debian `Build-Depends`, as is libsystemd
for the KDE bridge) and installed into the main package; once it renders scenes (plan phases P1 to P9) the
linux-wallpaperengine dependency goes away. `-DDESKTOP_CANVAS_BUNDLE_LWE=ON` (see
`cmake/BundleLwe.cmake`) exists for cases where no package is available, but
it downloads sources at build time, so it cannot be used by Debian, Fedora
or OBS, which build without network access. In practice that means scene
wallpapers work out of the box only where linux-wallpaperengine is packaged
separately, which today is essentially just the AUR.

`third_party/` holds vendored single header copies of nlohmann json and
CLI11. Debian and Fedora both prohibit convenience copies and package both
libraries themselves, so those two targets need the build taught to prefer
system copies before submission.

`claude_tests/` is untracked, so release tarballs made with `git archive`
contain no tests and `DESKTOP_CANVAS_TESTS` turns itself off. Packaging
recipes that want a test step (Fedora's `%check`, for example) need the
directory tracked and shipped in the tarball first.
