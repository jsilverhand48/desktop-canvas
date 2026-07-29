// Implementation of EglContext declared in EglContext.h.
#include "gl/EglContext.h"

#include <EGL/eglext.h>

#include "core/Log.h"

namespace canvas {

EglContext::~EglContext() {
    if (display_ != EGL_NO_DISPLAY) {
        if (context_ != EGL_NO_CONTEXT) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           EGL_NO_CONTEXT);
            eglDestroyContext(display_, context_);
        }
        eglTerminate(display_);
    }
}

bool EglContext::init(EGLenum platform, void* nativeDisplay) {
    auto getPlatformDisplay =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay) {
        display_ = getPlatformDisplay(platform, nativeDisplay, nullptr);
    } else {
        display_ = eglGetDisplay(
            static_cast<EGLNativeDisplayType>(nativeDisplay));
    }
    if (display_ == EGL_NO_DISPLAY) {
        log::error() << "eglGetPlatformDisplay failed";
        return false;
    }
    EGLint major, minor;
    if (!eglInitialize(display_, &major, &minor)) {
        log::error() << "eglInitialize failed";
        return false;
    }
    log::debug() << "EGL " << major << "." << minor;

    if (!eglBindAPI(EGL_OPENGL_API)) {
        log::error() << "eglBindAPI(EGL_OPENGL_API) failed";
        return false;
    }
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE,
    };
    EGLint num_configs = 0;
    if (!eglChooseConfig(display_, config_attribs, &config_, 1,
                         &num_configs) ||
        num_configs == 0) {
        log::error() << "eglChooseConfig found no config";
        return false;
    }
    const EGLint ctx_attribs[] = {EGL_NONE};
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, ctx_attribs);
    if (context_ == EGL_NO_CONTEXT) {
        log::error() << "eglCreateContext failed";
        return false;
    }
    return true;
}

EGLint EglContext::nativeVisualId() const {
    EGLint id = 0;
    eglGetConfigAttrib(display_, config_, EGL_NATIVE_VISUAL_ID, &id);
    return id;
}

EGLSurface EglContext::createWindowSurface(void* nativeWindow) {
    EGLSurface surface = eglCreateWindowSurface(
        display_, config_,
        reinterpret_cast<EGLNativeWindowType>(nativeWindow), nullptr);
    if (surface == EGL_NO_SURFACE) {
        log::error() << "eglCreateWindowSurface failed: " << eglGetError();
        return EGL_NO_SURFACE;
    }
    // Never block in eglSwapBuffers; pacing uses frame callbacks/timers.
    eglMakeCurrent(display_, surface, surface, context_);
    eglSwapInterval(display_, 0);
    return surface;
}

void EglContext::destroySurface(EGLSurface surface) {
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display_, surface);
}

bool EglContext::makeCurrent(EGLSurface surface) {
    return eglMakeCurrent(display_, surface, surface, context_) == EGL_TRUE;
}

void EglContext::swapBuffers(EGLSurface surface) {
    eglSwapBuffers(display_, surface);
}

}  // namespace canvas
