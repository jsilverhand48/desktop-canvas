# src/config/

`Config`: the daemon configuration. JSON schema is documented in Config.h.
Precedence: defaults < config file < CLI flags (merged in main.cpp).
Runtime changes (assignments, mute, broken list, playlist settings) are saved
back to the file the config was loaded from (`sourcePath`), atomically via
temp file rename.

The `playlist` block (file, interval, outputs) holds rotation settings only.
The wallpaper a rotation tick picks is deliberately not persisted: it lives
in InstanceManager's runtime override map, so `assignments` keeps meaning the
user's own choice and the config file is not rewritten every interval.

`Config::expandUser` expands a leading `~/` against `$HOME` for hand written
paths (used for `playlist.file` and `--playlist`). `~user` is not expanded.
