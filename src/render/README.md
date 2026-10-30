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
  On Wayland, outputs resolving to the same scene wallpaper share a single
  linux-wallpaperengine process (one `--screen-root` per output), so three
  screens showing one scene load its assets and GL context once instead of
  three times. Slots of a merged group hold the same shared instance; any
  change to the group (an output switching wallpaper, appearing, or
  vanishing) restarts the whole group with the new split, and a crash
  blanks every output in it. pause/resume/mute act once per instance.
  Video, web, and X11 scene instances stay one per output.
  Wayland scenes are started with `--layer <config.layer>` when main's
  startup probe found the flag (`setSceneLayerSupported`), so they share
  the video surfaces' layer instead of lwe's `bottom` default, which covers
  the Plasma desktop icons.
  Every scene launch passes the wallpaper's pinned property values, and with
  `scene.randomize_options` on one random value per other dropdown (limited
  to its allowed list), via `source/SceneOptions`, as `--set-property`. The
  values are logged and captured in the instance's argv builder, so an
  audio restart keeps the same variant. `restartAll()` relaunches every
  instance (after fps / scaling / engine changes), `restartWallpaper(id)`
  relaunches one (new random variant, new pinned values), and
  `clearBroken(id)` removes an id from the blacklist. `rescan()` records the
  scanned `roots()`, bumps `libraryRevision()` (the settings UI reloads its
  grid when it changes) and calls the `setOnRescan` listener, which main
  uses to keep `source/LibraryWatcher` pointed at the current roots.
  Video RenderSurfaces live in a per output map, separate from instance
  slots: created on the first video for an output, hidden between videos,
  and replaced when the output disappears or the surface reports
  `closed()`. On Wayland that is after every video, since KWin closes a
  layer surface that is remapped (the cause of black screens after
  switching back to a video), so each video gets a fresh surface; X11
  windows are reused. The per video leak once blamed on surface churn came
  from freeing mpv without its GL context current (see VideoBackend below).
  Video instances are built by an injected `VideoFactory`
  (`setVideoFactory`): main.cpp installs `VideoInstance` when compiled with
  libmpv, tests install fakes, and without a factory a video assignment is
  logged and leaves the output blank. InstanceManager therefore links no
  display or video library and lives in the `canvas_daemon` library;
  `claude_tests/test_video_factory` drives its apply() against
  `claude_tests/fakes/FakePlatform.h`.
- `Rotator` timed rotation over the enabled wallpapers in config: every
  `playlist.interval` seconds it draws a random id (skipping ids missing from the library,
  unsupported, or blacklisted) and installs it as a runtime override on
  InstanceManager, which reconciles as usual. All rotating outputs show the
  same pick; `playlist.outputs` narrows which outputs rotate, and an empty
  list installs the override under `*` so hotplugged outputs join
  immediately. Picks are never written to config.json, so stopping rotation
  restores the user's own assignments. `refresh()` rebuilds the pool after
  wallpapers are enabled or disabled without forcing a pick (a running
  rotation keeps its schedule). With one usable wallpaper and random
  variants on, a tick relaunches it so a new variant shows.
- `VideoBackend` (`VideoInstance`), compiled into the `desktop-canvas`
  executable only (`CANVAS_HAVE_VIDEO`, set when libmpv is found), not into
  `canvas_daemon`. libmpv render API into a RenderSurface:
  update callback bridged via eventfd, frames rendered only when both mpv
  has a new frame and the compositor signaled readiness. Mute toggles live
  via the mpv "mute" property. Teardown makes the surface current before
  `mpv_render_context_free()`: libmpv requires the creating GL context to be
  current for every `mpv_render_*` call, and free is where it deletes its
  textures, FBOs and hwdec interop resources. Freeing without it leaks a few
  hundred MB per instance, which rotation turns into an OOM kill within days.
  The surface is borrowed: start() calls show(), stop() frees mpv while the
  surface is still ready and then calls hide(). After
  `mpv_terminate_destroy()` it calls `malloc_trim(0)`: mpv's ~60 threads per
  instance leave freed memory parked in glibc malloc arenas, and without the
  trim (plus the arena cap set in main.cpp) daemon RSS grew ~100 MB per
  video played and was never returned to the system.
- `web/` optional Qt6 WebEngine backend (M6, DESKTOP_CANVAS_WEB=ON).
