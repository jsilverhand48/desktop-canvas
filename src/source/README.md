# src/source/

Workshop content scanning and the user's rotation playlist.

- `WallpaperInfo` value type: id, type (scene/video/web/unsupported), title,
  payload file.
- `WorkshopScanner` walks content roots (directories shaped like
  `steamapps/workshop/content/431960`), parses each `project.json`
  tolerantly (case insensitive type, presets with a "dependency" key and
  malformed items become Unsupported entries, never errors), autodetects
  default Steam library locations and the Wallpaper Engine `assets`
  directory required by scene rendering.
- `Playlist` reads a plain text list of wallpaper ids (one per line, `#`
  comments, blanks and whitespace ignored, duplicates collapsed) and draws
  random picks from it. Picks are filtered through a caller supplied
  predicate, so ids that are not in the library are skipped at draw time
  rather than dropped on load, and the id currently showing is excluded so a
  rotation tick always changes something. A failed reload keeps the previous
  contents so a typo cannot disarm a running rotation. Used by
  `render/Rotator`.
