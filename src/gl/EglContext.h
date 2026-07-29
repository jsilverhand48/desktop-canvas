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
