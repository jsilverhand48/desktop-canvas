# src/core/

Foundation utilities with no project dependencies (library `canvas_core`).

- `EventLoop` single threaded epoll loop: fd watches (`addFd`, `modifyFd`,
  `removeFd`), timerfd timers (relative `addTimer`, absolute deadline
  `addTimerAt` for drift free frame pacing), signalfd termination handling,
  and `post()`, the one thread safe entry point (an eventfd woken queue for
  handing work back from other threads). Everything in the daemon and its
  helpers (Wayland fd, mpv wakeups, pidfds, IPC sockets, control channels)
  runs through it. Callbacks are copied before invocation so a callback may
  safely remove its own fd.
- `ControlChannel` message framing over a SOCK_SEQPACKET unix socket: one
  datagram per message (64 KiB max), never blocking sends with a bounded
  queue drained on EPOLLOUT, oversized messages refused or dropped, a
  single closed notification on peer EOF or error. Callbacks may destroy the
  channel. The daemon and its helper processes layer JSON on it
  (`process/HelperProtocol.h`).
- `Log` leveled stderr logging (CANVAS_LOG=error|warn|info|debug).
