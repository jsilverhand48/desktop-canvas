# src/config/

`Config`: the daemon configuration. JSON schema is documented in Config.h.
Precedence: defaults < config file < CLI flags (merged in main.cpp).
Runtime changes (assignments, mute, broken list, playlist settings) are saved
back to the file the config was loaded from (`sourcePath`), atomically via
temp file rename.

`wallpapers` holds per wallpaper choices keyed by workshop id: `enabled`
(the rotation pool, replacing the old external ids file), `properties`
(pinned `--set-property` values, kept as text) and `allowed` (per dropdown,
the values random picks may use). Entries with nothing set are not written.
A legacy `playlist.file` is imported once by `load()` when nothing is
enabled yet (`importedPlaylistFrom`, saved by main.cpp); the key is not
written back. `ui` configures the settings page (enabled, port, blur of
Mature/Questionable previews).

The `playlist` block (interval, outputs) holds rotation settings only.
The wallpaper a rotation tick picks is deliberately not persisted: it lives
in InstanceManager's runtime override map, so `assignments` keeps meaning the
user's own choice and the config file is not rewritten every interval.

`audio.processing` (default true) lets native scenes react to what the system plays: while on, the
daemon captures the default output's monitor whenever a running scene
needs it (`src/audio/README.md`). Applied live. `audio.auto_mute`
(default true) mutes native scenes and
videos while another application plays audio. `audio.media` (default
true) lets scene scripts receive what media players play
(`src/scene/audio/README.md` section 7).

`scene.randomize_options` (default false) makes every scene launch pick a
random value for each dropdown property; `scene.include_nsfw_options`
(default false) lets those picks include options labelled NSFW / nude.

Scenes always render with desktop-canvas-scene. `scene.disable_mouse`
keeps the cursor from them. Older configs may still hold `scene.engine`,
`scene.engine_path` and per wallpaper `engine` (linux-wallpaperengine
choices, removed in plan phase P9): `load()` turns them into
`migrationNotes`, main.cpp logs those once and saves the config without
them.

`fullscreen_pause` (enabled, default true; `only_active`; `ignore_app_ids`
substrings) feeds `desktop/FullscreenPolicy`. `kde_bridge` (default true)
lets main.cpp start the KDE bridge on Plasma Wayland; it applies at the next
daemon start. `spans` lists output groups that share one canvas;
`validSpans()` requires two or more distinct non empty outputs per entry and
no output in two entries, and `load()` drops any entry that breaks that.

`Config::expandUser` expands a leading `~/` against `$HOME` for hand written
paths (used for `playlist.file` and `--playlist`). `~user` is not expanded.
