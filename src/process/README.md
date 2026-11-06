# src/process/

Supervised child processes: desktop-canvas-scene (the scene helper) and
desktop-canvas-web.

- `ChildSupervisor` fork/exec, pidfd based exit notification on the event
  loop, respawn with exponential backoff (1s..60s), give up after 5 rapid
  failures within 2 minutes and report through onBroken. Children start
  from a `ChildSpec`: each leads its own process group (stop sends SIGTERM,
  then SIGKILL after 3 s, to the whole group, so helpers' own subprocesses
  go too), gets PR_SET_PDEATHSIG so it dies with the daemon, and inherits
  only stdio plus, with `controlChannel`, its end of a fresh socketpair as
  fd 3 (`onSpawn` hands the daemon's end to the owner) and, with
  `inheritFd`, one more descriptor as fd 4 (the audio spectrum memory). Exit policy:
  `RespawnAlways` (web: any exit respawns) or `HelperProtocol` (exit 0
  clean, 2 usage and 3 unsupported end the child for good, 3 without
  counting as a failure; anything else is a crash). `onExit` reports the
  classified exit before any respawn.
- `HelperProtocol.h` the daemon/helper protocol: transport (ControlChannel
  on fd 3, the spectrum memory on fd 4), every message type in both
  directions (helpers ask for audio capture with "audio"), exit codes, heartbeat
  timing, and a decoder that rejects non objects and JSON nested deeper
  than 16 levels before parsing (helper messages are untrusted input).
- `HelperLocator` finds helper executables: `<NAME>_PATH` override (for
  example DESKTOP_CANVAS_SCENE_PATH), next to the daemon binary, then PATH.
- `ExternalProcessInstance` adapts a supervised web child to the
  WallpaperInstance interface. Mute changes restart the child because
  mute is a command line flag there.
- `SceneHelperInstance` WallpaperInstance for desktop-canvas-scene, tagged
  `native:<first output>`. Pause, resume, mute, volume, pointer and
  per output coverage by fullscreen windows are live messages (pointer
  coalesced to one update per 16 ms); state is re-sent after every hello,
  so respawned helpers pick it up. `Launch::disableMouse` (config
  `scene.disable_mouse`) keeps the pointer to itself. `Launch::span` adds `--span`;
  `Launch::audioFd` hands the spectrum memory over with `--audio-fd 4`,
  and `setOnAudioDemand` reports whether the helper needs the capture
  (its scene reads audio and it runs unpaused; false on exit and stop).
  `setMedia` sends the media players' state as "media" (re-sent after
  every hello, like the rest of the state). No
  message for 10 s means the helper hung: SIGKILL, which the supervisor
  counts as a crash.
  An unsupported report (strict helpers only; the daemon runs them
  non-strict since plan phase P9) fires `onUnsupported` (deferred one loop
  iteration); InstanceManager logs it.
