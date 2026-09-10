# src/gl/

`EglContext`: EGL display initialization (Wayland or X11 platform), a single
shared desktop GL context, and per output window surfaces. Swap interval is
forced to 0; frame pacing is the responsibility of RenderSurface
implementations (compositor frame callbacks or timers).

Because the context is shared, surface teardown has an ordering requirement:
`releaseCurrent()` before `destroySurface()`. EGL only flags a still-current
surface for deletion, and nothing makes another surface current when the
replacement wallpaper is a child process rather than an in-process backend,
so skipping the release keeps the surface and its colour buffers alive for
the remaining life of the daemon.
