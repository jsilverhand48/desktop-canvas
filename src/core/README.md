# src/core/

Foundation utilities with no project dependencies.

- `EventLoop` single threaded epoll loop: fd watches, timerfd timers,
  signalfd termination handling. Everything in the daemon (Wayland fd, mpv
  wakeups, pidfds, IPC sockets) runs through it. Callbacks are copied before
  invocation so a callback may safely remove its own fd.
- `Log` leveled stderr logging (CANVAS_LOG=error|warn|info|debug).
