# src/ipc/

Control interface.

- `Protocol.h` socket path resolution and the JSON lines request/response
  protocol documentation. Shared with tools/canvasctl.
- `IpcServer` nonblocking unix stream socket on the event loop; multiple
  clients; one JSON object per line each way.

Commands: status, list, set, enable, disable, playlist, rotate, next, pause,
resume, mute, unmute, reload, restart, quit. Dispatch lives in
`control/Controller::handleIpc`, shared with the settings UI.

- `enable` / `disable` add or remove ids (`"ids": [...]`) from the rotation
  pool (config `wallpapers.<id>.enabled`).
- `playlist` imports an ids file into the enabled set (additive), or with
  `""` disables every wallpaper. The file is not remembered.
- `rotate` sets the interval in seconds (0 stops rotation) and optionally the
  outputs it applies to (`[]` = every output, all showing the same pick).
- `next` applies the next random pick immediately and restarts the interval.
- `status` also reports how many wallpapers are enabled, the interval,
  whether rotation is running, seconds until the next tick, the rotating
  output list, and the settings UI address.
