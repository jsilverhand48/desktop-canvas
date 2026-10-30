# tools/canvasctl/

desktop-canvasctl: sends one JSON request line to the daemon socket and
prints the reply (human readable by default, raw with --json).

Commands: status, list, set <output|*> <id|none>, enable <id ...>,
disable <id ...>, playlist <file|none> (import an ids file into the enabled
set | disable every wallpaper), rotate <seconds|off> [output ...], next,
pause, resume, mute, unmute, reload, restart, quit. `status` also prints the
enabled count and the settings UI address.

Rotation intervals accept a bare number of seconds or a suffixed duration
(`30s`, `15m`, `2h`), plus `off` for 0. `rotate` with no output names rotates
every output with the same wallpaper; naming outputs limits rotation to them
and leaves the rest on their configured wallpaper.
