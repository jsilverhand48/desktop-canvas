# packaging/arch/

Source of truth for the AUR package. The AUR repository is a *separate* git
repo; these files are copied into it rather than pushed from here.

## Two different LICENSE files

This trips people up, so to be explicit:

- `/LICENSE` at the project root is MIT, the licence of desktop-canvas
  itself. It is what the `license=(MIT BSD-3-Clause)` field in the PKGBUILD
  describes, and it is installed into `/usr/share/licenses/desktop-canvas/`.
- `packaging/arch/LICENSE` is 0BSD, and licenses *the packaging sources*
  (the PKGBUILD itself). Arch RFC40 requires this, and packages carrying
  anything other than 0BSD here are not eligible for promotion from the AUR
  into the official repositories. `REUSE.toml` records the same thing in
  machine readable form; verify with `pkgctl license check` from devtools.

## Files that belong in the AUR repo

```
PKGBUILD
.SRCINFO
LICENSE       (the 0BSD one from this directory)
REUSE.toml
.gitignore
```

The AUR must not contain built packages, the filelist, or downloaded source
tarballs. The recommended `.gitignore` excludes everything and relies on
force-adding, which is why it is written out here rather than committed as a
real dotfile (it would otherwise apply to this repository too):

```gitignore
*
!.gitignore
!PKGBUILD
!.SRCINFO
!LICENSE
!REUSE.toml
!README.md
```

## Submitting

```
git -c init.defaultBranch=master clone ssh://aur@aur.archlinux.org/desktop-canvas.git
# copy PKGBUILD, LICENSE, REUSE.toml and the .gitignore above into it
updpkgsums                       # fills in sha256sums from the real release
makepkg --printsrcinfo > .SRCINFO
git add -f PKGBUILD .SRCINFO LICENSE REUSE.toml .gitignore
git commit -m "Initial import of desktop-canvas 1.0.0"
git push
```

Requires an SSH key registered on your AUR account, with
`Host aur.archlinux.org / User aur` configured in `~/.ssh/config`. Pushes are
only accepted on the `master` branch, and `.SRCINFO` must be in the same
commit as the `PKGBUILD` or the push is rejected.

Before pushing, build in a clean chroot and lint:

```
extra-x86_64-build              # devtools, catches missing makedepends
namcap PKGBUILD
namcap desktop-canvas-1.0.0-1-x86_64.pkg.tar.zst
```

## Outstanding before the first submission

- `sha256sums` is still `SKIP`. It cannot be computed until `v1.0.0` is
  tagged and the GitHub release tarball exists. Run `updpkgsums`
  (pacman-contrib) then, and regenerate `.SRCINFO`.
- The `# Maintainer:` line carries a placeholder GitHub noreply address.
  AUR convention is a contactable, lightly obfuscated address.

## Design notes

Split package. Building with `DESKTOP_CANVAS_WEB=ON` produces the
`desktop-canvas-web` helper, which pulls in qt6-webengine at runtime. That is
larger than everything else combined, so the helper ships as its own package
and the base package lists it in `optdepends`. Both are produced from a
single build.

`CMAKE_BUILD_TYPE=None`, not `Release`: the CMake package guidelines note
that `Release` appends `-O3` and overrides the `-O2` plus hardening flags
that come from `makepkg.conf`. Verified that `None` produces no `$srcdir`
references in the built package.

`DESKTOP_CANVAS_TESTS=OFF` and no `check()` function, because `claude_tests/`
is untracked and therefore absent from release tarballs. If that directory is
ever tracked, add a `check()` running `ctest --test-dir build`.

`desktop-canvas-scene` (the native scene renderer helper) is part of the
base package and adds `libepoxy`, `libpng`, `lz4`, `libjpeg-turbo`
(2.1 or later), `freetype2` and `fontconfig` (text layers) and
`quickjs-ng` (scene scripts) to `depends`;
`systemd-libs`
(sd-bus) backs the KDE bridge, `libpulse` the audio capture for audio
responsive scenes, whose KWin script installs to
`share/desktop-canvas/kwin/`. `.SRCINFO` lists them too.

Scene wallpapers need linux-wallpaperengine, which exists in the AUR only as
`linux-wallpaperengine-git`. That package declares
`provides=('linux-wallpaperengine')`, so the `optdepends` entry resolves, but
the description names the real package to save people a search.
