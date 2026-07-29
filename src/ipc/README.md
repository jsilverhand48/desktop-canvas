# src/ipc/

Control interface.

- `Protocol.h` socket path resolution and the JSON lines request/response
  protocol documentation. Shared with tools/canvasctl.
- `IpcServer` nonblocking unix stream socket on the event loop; multiple
  clients; one JSON object per line each way.

Commands: status, list, set, playlist, rotate, next, pause, resume, mute,
unmute, reload, quit.

- `playlist` sets (or clears, with `""`) the ids file used for rotation.
- `rotate` sets the interval in seconds (0 stops rotation) and optionally the
  outputs it applies to (`[]` = every output, all showing the same pick).
- `next` applies the next random pick immediately and restarts the interval.
- `status` also reports playlist path and size, interval, whether rotation is
  running, seconds until the next tick, and the rotating output list.
