# src/source/

Workshop content scanning and the user's rotation playlist.

- `WallpaperInfo` value type: id, type (scene/video/web/unsupported), title,
  payload file.
- `WorkshopScanner` walks content roots (directories shaped like
  `steamapps/workshop/content/431960`), parses each `project.json`
  tolerantly (case insensitive type, presets with a "dependency" key and
  malformed items become Unsupported entries, never errors), autodetects
  default Steam library locations and the Wallpaper Engine `assets`
  directory required by scene rendering. Library discovery
  (`defaultRootsFor`): every Steam install (native `~/.local/share/Steam`
  and `~/.steam/steam`, Flatpak `~/.var/app/com.valvesoftware.Steam/...`,
  snap `~/snap/steam/common/...`), every library each install's
  `steamapps/libraryfolders.vdf` lists (`parseLibraryFolders`), and the
  `/run/media/$USER/*/SteamLibrary` heuristic; existing workshop dirs only,
  duplicates through symlinks dropped.
- `Playlist` the rotation pool: `setIds()` from the enabled wallpapers in
  config, or `load()` of a plain text ids file (one per line, `#` comments,
  blanks and whitespace ignored, duplicates collapsed) for importing the
  legacy file. Picks are filtered through a caller supplied predicate, so
  ids that are not in the library are skipped at draw time rather than
  dropped, and the id currently showing is excluded so a rotation tick
  always changes something. A failed load keeps the previous contents.
  Used by `render/Rotator`, `config/Config` (import) and `control/`.
- `SceneOptions` reads every user property a scene declares in
  `project.json` (dropdowns, sliders, toggles, colours, text, headings) for
  the settings UI, and `resolveSceneProperties()` builds the
  `--set-property` list for one launch: pinned values always; with
  randomize on, one random value per other dropdown from its allowed list
  (or every option minus NSFW / nude unless allowed globally). Dropdowns
  with nothing to vary keep the wallpaper default, except that a lone
  remaining option is still set so an excluded default cannot show.
  Tolerant of missing or malformed files.
- `WallpaperInfo` also carries the item's content rating and tags (for the
  settings UI's blur and filters).
- `LibraryWatcher` inotify on every content root (item folders appearing,
  vanishing or renamed) and every item folder (its project.json written,
  moved in or deleted, which is when a Steam download becomes usable).
  Changes are debounced (1.5 s quiet, at most 10 s) into one callback; main
  rescans, re-applies and re-points the watcher (watch() adds and drops
  watches by difference). Roots that are missing or lost (drive unplugged)
  are re-checked every 30 s, and with autodetected roots a roots provider
  (`WorkshopScanner::defaultRoots`) is polled so a library drive mounted
  after startup is picked up.
