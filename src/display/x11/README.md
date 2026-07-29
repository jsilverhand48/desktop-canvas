# src/display/x11/

X11 implementation of the display abstractions.

- `X11Connection` display connection, XRandR CRTC enumeration with
  connector names, RRScreenChangeNotify hotplug tracking, per window event
  dispatch, and desktop demotion of scene child windows (MapNotify +
  _NET_WM_PID match -> stamp _NET_WM_WINDOW_TYPE_DESKTOP + lower).
- `X11DesktopOutput` RenderSurface on an EWMH DESKTOP type window per CRTC,
  created with the EGL config's native visual (avoids EGL_BAD_MATCH),
  skip taskbar/pager states, timer based frame pacing (no compositor
  callbacks on X11).
- `X11Platform` ties everything together, owns the shared EglContext and
  the pointer poller.

Note: on DEs whose icon component is itself an opaque DESKTOP window
(xfdesktop, plasmashell, caja, nemo), disable the DE's desktop drawing to
see the wallpaper; see the README caveat matrix.
