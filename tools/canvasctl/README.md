# tools/canvasctl/

desktop-canvasctl: sends one JSON request line to the daemon socket and
prints the reply (human readable by default, raw with --json).

Commands: status, list, set <output|*> <id|none>, playlist <file|none>,
rotate <seconds|off> [output ...], next, pause, resume, mute, unmute, reload,
quit.

Rotation intervals accept a bare number of seconds or a suffixed duration
(`30s`, `15m`, `2h`), plus `off` for 0. `rotate` with no output names rotates
every output with the same wallpaper; naming outputs limits rotation to them
and leaves the rest on their configured wallpaper.
