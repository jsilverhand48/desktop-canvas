# src/process/

Supervised child processes: linux-wallpaperengine (lwe) for scenes on the
"lwe" engine, desktop-canvas-scene (the native scene helper, engines
"native" and "auto") and desktop-canvas-web.

- `LweCommandBuilder` builds the lwe argv (verified flags: --screen-root,
  --bg, --window, --silent, --volume, --fps, --scaling, --assets-dir,
  --disable-mouse) and probes `--help` to detect flag skew. InstanceManager
  runs the probe lazily, the first time an lwe scene starts.
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
  failures within 2 minutes and report through onBroken. Children start
  from a `ChildSpec`: each leads its own process group (stop sends SIGTERM,
  then SIGKILL after 3 s, to the whole group, so helpers' own subprocesses
  go too), gets PR_SET_PDEATHSIG so it dies with the daemon, and inherits
  only stdio plus, with `controlChannel`, its end of a fresh socketpair as
  fd 3 (`onSpawn` hands the daemon's end to the owner). Exit policy:
  `RespawnAlways` (lwe, web: any exit respawns) or `HelperProtocol` (exit 0
  clean, 2 usage and 3 unsupported end the child for good, 3 without
  counting as a failure; anything else is a crash). `onExit` reports the
  classified exit before any respawn.
- `HelperProtocol.h` the daemon/helper protocol: transport (ControlChannel
  on fd 3), every message type in both directions, exit codes, heartbeat
  timing, and a decoder that rejects non objects and JSON nested deeper
  than 16 levels before parsing (helper messages are untrusted input).
- `HelperLocator` finds helper executables: `<NAME>_PATH` override (for
  example DESKTOP_CANVAS_SCENE_PATH), next to the daemon binary, then PATH.
- `ExternalProcessInstance` adapts a supervised lwe or web child to the
  WallpaperInstance interface. Mute changes restart the child because mute
  is a command line flag (--silent) in linux-wallpaperengine. A scene merged
  across outputs is tagged `scene:<first output>`.
- `SceneHelperInstance` WallpaperInstance for desktop-canvas-scene, tagged
  `native:<first output>`. Pause, resume, mute, volume and pointer are live
  messages (pointer coalesced to one update per 16 ms); state is re-sent
  after every hello, so respawned helpers pick it up. No message for 10 s
  means the helper hung: SIGKILL, which the supervisor counts as a crash.
  A strict helper's unsupported report fires `onUnsupported` (deferred one
  loop iteration) so InstanceManager can fall back to lwe.
