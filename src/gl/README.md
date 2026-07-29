# src/gl/

`EglContext`: EGL display initialization (Wayland or X11 platform), a single
shared desktop GL context, and per output window surfaces. Swap interval is
forced to 0; frame pacing is the responsibility of RenderSurface
implementations (compositor frame callbacks or timers).
