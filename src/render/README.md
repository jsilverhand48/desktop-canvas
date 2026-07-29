# src/render/

Wallpaper instance orchestration and in-process backends.

- `WallpaperInstance` per (output, wallpaper) lifecycle interface:
  start/stop/pause/resume/setMuted/setPointer.
- `InstanceManager` owns scan results and running instances; reconciles
  config assignments against present outputs (apply()), services IPC
  commands, persists changes, and blacklists scene wallpapers whose child
  process keeps crashing (config "broken" list). Resolution order for an
  output is: runtime override (exact name, then `*`), then the config
  assignment (exact name, then `*`). Overrides belong to Rotator and are
  never persisted; an IPC `set` mirrors itself into the override map so a
  manual choice is visible until the next rotation tick reclaims the output.
- `Rotator` timed rotation over a playlist file: every `playlist.interval`
  seconds it draws a random id (skipping ids missing from the library,
  unsupported, or blacklisted) and installs it as a runtime override on
  InstanceManager, which reconciles as usual. All rotating outputs show the
  same pick; `playlist.outputs` narrows which outputs rotate, and an empty
  list installs the override under `*` so hotplugged outputs join
  immediately. Picks are never written to config.json, so stopping rotation
  restores the user's own assignments.
- `VideoBackend` (`VideoInstance`) libmpv render API into a RenderSurface:
  update callback bridged via eventfd, frames rendered only when both mpv
  has a new frame and the compositor signaled readiness. Mute toggles live
  via the mpv "mute" property.
- `web/` optional Qt6 WebEngine backend (M6, DESKTOP_CANVAS_WEB=ON).
