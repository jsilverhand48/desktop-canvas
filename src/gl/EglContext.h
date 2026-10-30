// Thin EGL wrapper shared by the Wayland and X11 paths.
//
// One EglContext per process holds the EGLDisplay and a single shared
// EGLContext (desktop OpenGL via EGL_OPENGL_API). Each output creates an
// EGLSurface from its native window (wl_egl_window or X11 Window). Swap
// interval is forced to 0; frame pacing is done with compositor frame
// callbacks or timers, never by blocking in eglSwapBuffers.
//
// Options select the context: the daemon keeps the defaults (driver default
// version and profile, 8 bit alpha) because libmpv shares its context; the
// scene helper asks for GL 3.3 core, robust access and no alpha. initDevice()
// is the headless variant: an EGL device (EGL_EXT_platform_device) with a
// surfaceless context, used for screenshots and GPU tests without a display
// server; everything is then rendered into framebuffer objects.
#pragma once

#include <EGL/egl.h>

#include <string>
#include <vector>

namespace canvas {

// Context selection for EglContext (namespace scope so default arguments
// can use it; spelled EglContext::Options at call sites).
struct EglOptions {
    // Desktop GL version to request; 0.0 keeps the driver default.
    int major = 0;
    int minor = 0;
    bool core = false;    // core profile (needs 3.2 or later)
    bool robust = false;  // robust access, lose the context on GPU reset
                          // (falls back to a plain context if refused)
    bool debug = false;   // debug context for KHR_debug output
    bool alpha = true;    // 8 bit alpha channel in the window config
};

class EglContext {
public:
    using Options = EglOptions;

    // An EGL device for initDevice(): its DRM node (empty for software
    // devices) and, when known, the renderer string of a context on it.
    struct DeviceInfo {
        int index = 0;
        std::string drmNode;
        std::string renderer;
    };

    EglContext() = default;
    ~EglContext();
    EglContext(const EglContext&) = delete;
    EglContext& operator=(const EglContext&) = delete;

    // platform: EGL_PLATFORM_WAYLAND_KHR or EGL_PLATFORM_X11_KHR.
    // nativeDisplay: wl_display* or Display*.
    bool init(EGLenum platform, void* nativeDisplay,
              const Options& options = {});

    // Headless init on an EGL device. name picks the device: empty for the
    // first that works, a decimal index, or a case insensitive substring of
    // its DRM node or renderer ("llvmpipe", "radeonsi", "NVIDIA"). The
    // context is made current without a surface on success.
    bool initDevice(const std::string& name, const Options& options = {});
    // Every EGL device with the renderer each one reports (probing creates a
    // throwaway context per device).
    static std::vector<DeviceInfo> listDevices();

    // GL_RENDERER of the context, for logs; needs the context current.
    std::string renderer() const;
    // Binds the context with no surface (headless rendering into FBOs).
    bool makeCurrentSurfaceless();

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
    // Picks a config and creates the context on display_ (already
    // initialized). surfaceType is EGL_WINDOW_BIT or EGL_PBUFFER_BIT.
    bool createContext(const Options& options, EGLint surfaceType);

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
};

}  // namespace canvas
