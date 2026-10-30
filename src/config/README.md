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

`scene.randomize_options` (default false) makes every scene launch pick a
random value for each dropdown property; `scene.include_nsfw_options`
(default false) lets those picks include options labelled NSFW / nude.

`Config::expandUser` expands a leading `~/` against `$HOME` for hand written
paths (used for `playlist.file` and `--playlist`). `~user` is not expanded.
