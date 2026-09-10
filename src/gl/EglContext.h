// Thin EGL wrapper shared by the Wayland and X11 paths.
//
// One EglContext per daemon holds the EGLDisplay and a single shared
// EGLContext (desktop OpenGL via EGL_OPENGL_API). Each output creates an
// EGLSurface from its native window (wl_egl_window or X11 Window). Swap
// interval is forced to 0; frame pacing is done with compositor frame
// callbacks or timers, never by blocking in eglSwapBuffers.
#pragma once

#include <EGL/egl.h>

namespace canvas {

class EglContext {
public:
    EglContext() = default;
    ~EglContext();
    EglContext(const EglContext&) = delete;
    EglContext& operator=(const EglContext&) = delete;

    // platform: EGL_PLATFORM_WAYLAND_KHR or EGL_PLATFORM_X11_KHR.
    // nativeDisplay: wl_display* or Display*.
    bool init(EGLenum platform, void* nativeDisplay);

    EGLSurface createWindowSurface(void* nativeWindow);
    void destroySurface(EGLSurface surface);

    bool makeCurrent(EGLSurface surface);
    void swapBuffers(EGLSurface surface);

    // Unbind the shared context from whatever surface is current.
    // eglDestroySurface only *flags* a still-current surface for deletion, so
    // an output must release the context before destroying its surface, or
    // the driver keeps the colour buffers alive until some other surface is
    // made current. That never happens when the next wallpaper is a child
    // process rather than an in-process backend, so the surface leaks for the
    // remaining lifetime of the daemon.
    void releaseCurrent();

    EGLDisplay display() const { return display_; }
    bool valid() const { return context_ != EGL_NO_CONTEXT; }

    // X11 windows must be created with the visual matching the chosen EGL
    // config or eglCreateWindowSurface fails with EGL_BAD_MATCH.
    EGLint nativeVisualId() const;

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
};

}  // namespace canvas
