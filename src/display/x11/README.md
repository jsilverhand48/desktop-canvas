# src/display/x11/

X11 implementation of the display abstractions.

- `X11Connection` display connection, XRandR CRTC enumeration with
  connector names, RRScreenChangeNotify hotplug tracking, per window event
  dispatch, and desktop demotion of scene child windows (MapNotify +
  _NET_WM_PID match -> stamp _NET_WM_WINDOW_TYPE_DESKTOP + lower).
- `X11DesktopOutput` RenderSurface on an EWMH DESKTOP type window per CRTC,
  created with the EGL config's native visual (avoids EGL_BAD_MATCH),
  skip taskbar/pager states, timer based frame pacing on absolute
  deadlines at the platform fps (no compositor callbacks on X11; no drift,
  no catch up bursts).
- `X11Toplevels` other clients' windows for the fullscreen pause:
  `_NET_CLIENT_LIST`, `_NET_ACTIVE_WINDOW`, `_NET_WM_STATE_FULLSCREEN`,
  WM_CLASS, outputs from the root relative geometry; desktop and dock types
  skipped. Rescans 50 ms after root or client property / configure events,
  under an error trap (vanishing windows must not abort the process). Uses
  `X11Connection::addEventObserver` and `addRootEventMask`.
- `X11Platform` ties everything together, owns the shared EglContext and
  the pointer poller. Its constructor takes the `EglContext::Options` and
  the frame rate for the timer pacing (default 30; desktop-canvas-scene
  passes its --fps). `egl()` exposes the context.

Note: on DEs whose icon component is itself an opaque DESKTOP window
(xfdesktop, plasmashell, caja, nemo), disable the DE's desktop drawing to
see the wallpaper; see the README caveat matrix.
