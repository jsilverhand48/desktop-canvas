# src/gl/

`EglContext`: EGL display initialization (Wayland or X11 platform), a single
shared desktop GL context, and per output window surfaces. Swap interval is
forced to 0; frame pacing is the responsibility of RenderSurface
implementations (compositor frame callbacks or timers). Part of the
`canvas_display` library.

`EglContext::Options` (namespace scope `EglOptions`) selects the context:
GL version, core profile, robust access with lose-on-reset (falls back to a
plain context when refused), debug, and whether the window config has an
alpha channel. The daemon keeps the defaults (driver default version and
profile, 8 bit alpha) because libmpv shares its context;
desktop-canvas-scene asks for GL 3.3 core, robust, no alpha.

`initDevice(name)` is the headless path: it enumerates EGL devices
(EGL_EXT_device_enumeration, EGL_EXT_platform_device), picks one by index
or by a substring of its DRM node or GL renderer ("llvmpipe", "radeonsi",
"NVIDIA"), and makes the context current without a surface, so rendering
goes to framebuffer objects. `listDevices()` reports every device with its
renderer; on the development machine that is NVIDIA, a Mesa entry for the
NVIDIA node that cannot be used, radeonsi (the iGPU) and llvmpipe.
`renderer()` and the device helpers resolve their GL and EGL extension
entry points through eglGetProcAddress, so this file links only libEGL.

Because the context is shared, surface teardown has an ordering requirement:
`releaseCurrent()` before `destroySurface()`. EGL only flags a still-current
surface for deletion, and nothing makes another surface current when the
replacement wallpaper is a child process rather than an in-process backend,
so skipping the release keeps the surface and its colour buffers alive for
the remaining life of the daemon.
