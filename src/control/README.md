# src/control/

`Controller`: every daemon action and setting in one place, used by both the
IPC socket (`handleIpc`, desktop-canvasctl) and the browser settings UI
(`http/WebApi`), so each operation has a single implementation.

- Reads: `status()` (outputs, what they show, rotation, mute/pause, UI
  address, `library_revision` / `library_size` for the UI's auto reload), `library()` (every item with enabled / broken / customized /
  rating / tags / preview), `settings()` (all global settings),
  `wallpaper(id)` (one item, its user properties from project.json, saved
  pinned and allowed values, outputs showing it).
- Writes: `updateSettings()` validates the whole patch against a copy of
  Config before committing, so a bad field changes nothing. `updateWallpaper`,
  `setEnabled` (bulk), `show` (temporary override, not persisted) and
  `action` (next, pause, resume, mute, unmute, reload, restart).
- Every write persists config.json and triggers the live effect it needs;
  the mapping is documented at the top of `Controller.h` (instance
  restarts, rescans, `Rotator::refresh`, mute, assignments, blacklist).
  UI port / enabled changes return a note that they apply after a daemon
  restart.
