// Implementation of EglContext declared in EglContext.h.
//
// Device enumeration and the GL_RENDERER query go through eglGetProcAddress,
// so this file links only libEGL (no GL loader).
#include "gl/EglContext.h"

#include <EGL/eglext.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

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

bool EglContext::init(EGLenum platform, void* nativeDisplay,
                      const Options& options) {
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
    return createContext(options, EGL_WINDOW_BIT);
}

bool EglContext::createContext(const Options& options, EGLint surfaceType) {
    if (!eglBindAPI(EGL_OPENGL_API)) {
        log::error() << "eglBindAPI(EGL_OPENGL_API) failed";
        return false;
    }
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, surfaceType,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, options.alpha ? 8 : 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE,
    };
    // eglChooseConfig sorts deeper colour buffers first, so "alpha 0" still
    // returns alpha configs first; pick an alpha-less one explicitly.
    EGLint num_configs = 0;
    if (!eglChooseConfig(display_, config_attribs, nullptr, 0, &num_configs) ||
        num_configs == 0) {
        log::error() << "eglChooseConfig found no config";
        return false;
    }
    std::vector<EGLConfig> configs(static_cast<size_t>(num_configs));
    eglChooseConfig(display_, config_attribs, configs.data(), num_configs,
                    &num_configs);
    config_ = configs.front();
    if (!options.alpha) {
        for (EGLConfig candidate : configs) {
            EGLint alpha = -1;
            eglGetConfigAttrib(display_, candidate, EGL_ALPHA_SIZE, &alpha);
            if (alpha == 0) {
                config_ = candidate;
                break;
            }
        }
    }

    auto attribs = [&](bool robust) {
        std::vector<EGLint> a;
        if (options.major > 0) {
            a.insert(a.end(), {EGL_CONTEXT_MAJOR_VERSION, options.major,
                               EGL_CONTEXT_MINOR_VERSION, options.minor});
        }
        if (options.core)
            a.insert(a.end(), {EGL_CONTEXT_OPENGL_PROFILE_MASK,
                               EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT});
        if (options.debug) a.insert(a.end(), {EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE});
        if (robust)
            a.insert(a.end(), {EGL_CONTEXT_OPENGL_ROBUST_ACCESS, EGL_TRUE,
                               EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY,
                               EGL_LOSE_CONTEXT_ON_RESET});
        a.push_back(EGL_NONE);
        return a;
    };
    auto ctx = attribs(options.robust);
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, ctx.data());
    if (context_ == EGL_NO_CONTEXT && options.robust) {
        log::warn() << "robust GL context refused, using a plain one";
        ctx = attribs(false);
        context_ =
            eglCreateContext(display_, config_, EGL_NO_CONTEXT, ctx.data());
    }
    if (context_ == EGL_NO_CONTEXT) {
        char code[16];
        std::snprintf(code, sizeof(code), "0x%04x",
                      static_cast<unsigned>(eglGetError()));
        log::error() << "eglCreateContext failed: " << code;
        return false;
    }
    return true;
}

namespace {

struct DeviceApi {
    PFNEGLQUERYDEVICESEXTPROC queryDevices = nullptr;
    PFNEGLQUERYDEVICESTRINGEXTPROC queryDeviceString = nullptr;
    PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay = nullptr;

    DeviceApi() {
        queryDevices = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
            eglGetProcAddress("eglQueryDevicesEXT"));
        queryDeviceString = reinterpret_cast<PFNEGLQUERYDEVICESTRINGEXTPROC>(
            eglGetProcAddress("eglQueryDeviceStringEXT"));
        getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    }
    bool ok() const {
        return queryDevices && queryDeviceString && getPlatformDisplay;
    }
    std::vector<EGLDeviceEXT> devices() const {
        EGLint count = 0;
        if (!ok() || !queryDevices(0, nullptr, &count) || count <= 0) return {};
        std::vector<EGLDeviceEXT> list(static_cast<size_t>(count));
        queryDevices(count, list.data(), &count);
        list.resize(static_cast<size_t>(count));
        return list;
    }
    std::string drmNode(EGLDeviceEXT device) const {
#ifdef EGL_DRM_RENDER_NODE_FILE_EXT
        if (const char* s = queryDeviceString(device, EGL_DRM_RENDER_NODE_FILE_EXT))
            return s;
#endif
#ifdef EGL_DRM_DEVICE_FILE_EXT
        if (const char* s = queryDeviceString(device, EGL_DRM_DEVICE_FILE_EXT))
            return s;
#endif
        return {};
    }
};

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c));
    });
}

}  // namespace

bool EglContext::initDevice(const std::string& name, const Options& options) {
    DeviceApi api;
    if (!api.ok()) {
        log::error() << "EGL device enumeration (EGL_EXT_device_enumeration, "
                        "EGL_EXT_platform_device) is not available";
        return false;
    }
    auto devices = api.devices();
    const std::string wanted = lower(name);
    for (size_t i = 0; i < devices.size(); i++) {
        if (allDigits(name) && std::stoul(name) != i) continue;
        display_ = api.getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devices[i],
                                          nullptr);
        EGLint major = 0, minor = 0;
        if (display_ == EGL_NO_DISPLAY ||
            !eglInitialize(display_, &major, &minor)) {
            display_ = EGL_NO_DISPLAY;
            continue;
        }
        if (createContext(options, EGL_PBUFFER_BIT) &&
            makeCurrentSurfaceless()) {
            std::string node = api.drmNode(devices[i]);
            std::string rend = renderer();
            if (wanted.empty() || allDigits(name) ||
                lower(node).find(wanted) != std::string::npos ||
                lower(rend).find(wanted) != std::string::npos) {
                log::info() << "headless EGL device " << i << ": " << rend
                            << (node.empty() ? "" : " (" + node + ")");
                return true;
            }
        }
        if (context_ != EGL_NO_CONTEXT) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           EGL_NO_CONTEXT);
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }
    log::error() << "no usable EGL device"
                 << (name.empty() ? std::string() : " matching \"" + name + "\"");
    return false;
}

std::vector<EglContext::DeviceInfo> EglContext::listDevices() {
    std::vector<DeviceInfo> result;
    DeviceApi api;
    auto devices = api.devices();
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceInfo info;
        info.index = static_cast<int>(i);
        info.drmNode = api.drmNode(devices[i]);
        EglContext probe;
        if (probe.initDevice(std::to_string(i))) info.renderer = probe.renderer();
        result.push_back(std::move(info));
    }
    return result;
}

std::string EglContext::renderer() const {
    using GetString = const unsigned char* (*)(unsigned int);
    auto getString =
        reinterpret_cast<GetString>(eglGetProcAddress("glGetString"));
    if (!getString || context_ == EGL_NO_CONTEXT) return {};
    const unsigned char* s = getString(0x1F01);  // GL_RENDERER
    return s ? reinterpret_cast<const char*>(s) : std::string();
}

bool EglContext::makeCurrentSurfaceless() {
    return eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                          context_) == EGL_TRUE;
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

void EglContext::releaseCurrent() {
    if (display_ == EGL_NO_DISPLAY) return;
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void EglContext::swapBuffers(EGLSurface surface) {
    eglSwapBuffers(display_, surface);
}

}  // namespace canvas
