# packaging/

Distribution artifacts.

- `systemd/desktop-canvas.service.in` user unit template (ExecStart is
  filled with the install prefix at configure time); enable with
  `systemctl --user enable --now desktop-canvas`.
- `autostart/desktop-canvas.desktop` XDG autostart/menu entry.
- `flatpak/io.github.desktopcanvas.DesktopCanvas.yml` reference Flatpak
  manifest (KDE runtime for Qt6 WebEngine; bundles linux-wallpaperengine
  as a module; fill in the source sha256 before building).
- `arch/PKGBUILD` reference Arch package.
- `debian/` reference control and rules stubs for a Debian package.
- `plasma/` KDE Plasma integration (transparent wallpaper plugin, KWin
  script and setup script) so desktop icons render above the wallpaper; see
  plasma/README.md.

Scene wallpapers depend on linux-wallpaperengine at runtime; distro
packages should Recommend/Suggest it, or use
-DDESKTOP_CANVAS_BUNDLE_LWE=ON (see cmake/BundleLwe.cmake) where no
package exists.
