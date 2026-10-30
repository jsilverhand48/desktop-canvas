# src/process/

Scene wallpapers run as supervised linux-wallpaperengine child processes.

- `LweCommandBuilder` builds the argv (verified flags: --screen-root, --bg,
  --window, --silent, --volume, --fps, --scaling, --assets-dir,
  --disable-mouse) and probes `--help` at startup to detect flag skew.
  `LweOptions::screenRoots` takes several outputs; each gets its own
  `--screen-root X --bg <dir> --scaling <mode>` triple so one process
  renders the same wallpaper on all of them.
  `LweOptions::layer` adds `--layer <name>` (screen-root mode only).
  `supportsLayer()` checks the `--help` probe for it; lwe r611+ has the flag.
  lwe's own default has been `bottom` since r598, which KWin stacks above
  the Plasma desktop icons, so the daemon passes its configured layer.
  `LweOptions::properties` adds one `--set-property name=value` per entry
  (random scene variants, see `source/SceneOptions`).
- `ChildSupervisor` fork/exec, pidfd based exit notification on the event
  loop, respawn with exponential backoff (1s..60s), give up after 5 rapid
  failures within 2 minutes and report through onBroken.
- `ExternalProcessInstance` adapts a supervised child to the
  WallpaperInstance interface. Mute changes restart the child because mute
  is a command line flag (--silent) in linux-wallpaperengine. A scene merged
  across outputs is tagged `scene:<first output>`.
