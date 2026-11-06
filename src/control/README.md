# src/control/

`Controller`: every daemon action and setting in one place, used by both the
IPC socket (`handleIpc`, desktop-canvasctl) and the browser settings UI
(`http/WebApi`), so each operation has a single implementation.

- Reads: `status()` (outputs, what they show, rotation, mute/pause, UI
  address, `library_revision` / `library_size` for the UI's auto reload), `library()` (every item with enabled / broken / customized /
  rating / tags / preview), `settings()` (all global settings),
  `wallpaper(id)` (one item, its user properties from project.json with
  each display `condition` and whether it is `shown` for the pinned values,
  saved pinned and allowed values, outputs showing it).
- Writes: `updateSettings()` validates the whole patch against a copy of
  Config before committing, so a bad field changes nothing. `updateWallpaper`,
  `setEnabled` (bulk), `show` (temporary override, not persisted) and
  `action` (next, pause, resume, mute, unmute, reload, restart).
- Scenes: `settings()` reports `scene.native_available`
  (desktop-canvas-scene found); `scene.disable_mouse` changes restart every
  instance.
- Volume changes apply live (InstanceManager::setVolume); only backends that
  take volume at launch (web) are restarted, and only when unmuted.
- Desktop integration: `settings()` reports `fullscreen_pause`,
  `kde_bridge` and `spans`; `status()` gives every output its `x`, `y` and
  `covered` (a fullscreen app pauses it). Fullscreen rule changes apply live
  (`refreshCoverage`), span changes restart every instance (validated with
  `Config::validSpans`), and a `kde_bridge` change returns a note that it
  applies after a daemon restart.
- Every write persists config.json and triggers the live effect it needs;
  the mapping is documented at the top of `Controller.h` (instance
  restarts, rescans, `Rotator::refresh`, mute, assignments, blacklist).
  UI port / enabled changes return a note that they apply after a daemon
  restart.
