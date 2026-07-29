# src/process/

Scene wallpapers run as supervised linux-wallpaperengine child processes.

- `LweCommandBuilder` builds the argv (verified flags: --screen-root, --bg,
  --window, --silent, --volume, --fps, --scaling, --assets-dir,
  --disable-mouse) and probes `--help` at startup to detect flag skew.
- `ChildSupervisor` fork/exec, pidfd based exit notification on the event
  loop, respawn with exponential backoff (1s..60s), give up after 5 rapid
  failures within 2 minutes and report through onBroken.
- `ExternalProcessInstance` adapts a supervised child to the
  WallpaperInstance interface. Mute changes restart the child because mute
  is a command line flag (--silent) in linux-wallpaperengine.
