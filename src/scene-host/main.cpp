// desktop-canvas-scene: renders one Wallpaper Engine scene on one or more
// outputs. The daemon starts it (engine "native" or "auto", see
// render/InstanceManager.h) with --control-fd; it also runs by hand.
//
// Modes:
//   desktop-canvas-scene [options] WALLPAPER_DIR
//       show the scene on --output outputs (default all) as layer shell
//       surfaces (Wayland) or desktop windows (X11)
//   desktop-canvas-scene --screenshot FILE [--size WxH] [--frames N]
//                        [--device NAME] WALLPAPER_DIR
//       headless: render on an EGL device and write a PNG
//   desktop-canvas-scene --check WALLPAPER_DIR
//       print the support report as JSON and exit (0 supported, 3 not)
//   desktop-canvas-scene --list-devices
//       list EGL devices usable for --screenshot
//
// --strict (what "auto" passes) makes content that needs anything this
// build cannot render a reason to stop: the helper reports the list over
// the control channel and exits with helper::kExitUnsupported so the daemon
// falls back to linux-wallpaperengine. Without it ("native"), whatever is
// supported is shown and the rest skipped. The scene engine itself lands in
// plan phases P1 onward; until then nothing is supported and the helper
// shows a solid placeholder colour.
//
// Exit codes follow process/HelperProtocol.h: 0 clean, 2 usage, 3
// unsupported, 1 runtime failure (counted as a crash by the daemon).
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>

#include "core/ControlChannel.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "display/Platform.h"
#include "gl/EglContext.h"
#include "process/HelperProtocol.h"
#include "scene-host/PlaceholderRenderer.h"
#include "scene-host/Sandbox.h"
#include "scene-host/SceneHost.h"
#include "scene-host/Screenshot.h"

#ifdef CANVAS_HAVE_WAYLAND
#include "display/wayland/WaylandPlatform.h"
#endif
#ifdef CANVAS_HAVE_X11
#include "display/x11/X11Platform.h"
#endif

namespace fs = std::filesystem;
using namespace canvas;

namespace {

// Placeholder colour: a dark slate, distinct from black so a running
// helper is visible.
constexpr float kPlaceholder[3] = {0.09f, 0.10f, 0.13f};

// Features the wallpaper needs that this build cannot render yet. The
// engine phases replace this with the real capability report.
std::vector<std::string> unsupportedFeatures(const fs::path& /*dir*/) {
    return {"scene-renderer"};
}

EglContext::Options glOptions(bool debug) {
    EglContext::Options options;
    options.major = 3;
    options.minor = 3;
    options.core = true;
    options.robust = true;
    options.debug = debug;
    options.alpha = false;
    return options;
}

// Where the helper may still write once sandboxed.
std::vector<std::string> writablePaths(const std::string& extra) {
    std::vector<std::string> paths;
    const char* home = std::getenv("HOME");
    const char* cache = std::getenv("XDG_CACHE_HOME");
    if (cache && *cache)
        paths.emplace_back(cache);
    else if (home && *home)
        paths.push_back(std::string(home) + "/.cache");
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime)
        paths.emplace_back(runtime);
    paths.emplace_back("/tmp");
    paths.emplace_back("/dev/shm");
    if (!extra.empty()) paths.push_back(extra);
    return paths;
}

bool parseSize(const std::string& text, int& width, int& height) {
    auto x = text.find('x');
    if (x == std::string::npos) return false;
    try {
        width = std::stoi(text.substr(0, x));
        height = std::stoi(text.substr(x + 1));
    } catch (...) {
        return false;
    }
    return width > 0 && height > 0 && width <= 16384 && height <= 16384;
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"desktop-canvas-scene: native scene renderer for desktop-canvas"};
    std::string wallpaper;
    int controlFd = -1;
    std::vector<std::string> outputs;
    std::string layer = "background";
    int fps = 30;
    std::string scaling = "fill";
    std::string clamp = "clamp";
    std::string assetsDir;
    std::vector<std::string> properties;
    bool muted = false;
    int volume = 15;
    bool strict = false;
    bool check = false;
    std::string screenshot;
    std::string size = "1920x1080";
    int frames = 3;
    std::string device;
    bool listDevices = false;
    bool noSandbox = false;
    bool glDebug = false;

    app.add_option("wallpaper", wallpaper, "Wallpaper directory (has project.json)");
    app.add_option("--control-fd", controlFd, "Daemon control socket (HelperProtocol)");
    app.add_option("--output", outputs, "Output to cover (repeatable; default all)");
    app.add_option("--layer", layer, "Layer shell layer: background | bottom");
    app.add_option("--fps", fps, "Frame rate cap")->check(CLI::Range(1, 240));
    app.add_option("--scaling", scaling, "fill | fit | stretch | default");
    app.add_option("--clamp", clamp, "clamp | border | repeat");
    app.add_option("--assets-dir", assetsDir, "Wallpaper Engine assets directory");
    app.add_option("--set-property", properties, "NAME=VALUE user property (repeatable)");
    app.add_flag("--muted", muted, "Start muted");
    app.add_option("--volume", volume, "Volume 0..100")->check(CLI::Range(0, 100));
    app.add_flag("--strict", strict, "Exit 3 when anything is unsupported");
    bool span = false;
    app.add_flag("--span", span, "Stretch one canvas over every --output");
    app.add_flag("--check", check, "Print the support report and exit");
    app.add_option("--screenshot", screenshot, "Render headless into this PNG");
    app.add_option("--size", size, "Screenshot size WxH");
    app.add_option("--frames", frames, "Screenshot frames to render")->check(CLI::Range(1, 10000));
    app.add_option("--device", device, "EGL device for --screenshot (index or name)");
    app.add_flag("--list-devices", listDevices, "List EGL devices and exit");
    app.add_flag("--no-sandbox", noSandbox, "Do not apply the Landlock sandbox");
    app.add_flag("--gl-debug", glDebug, "Create a GL debug context");
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        int code = app.exit(e);
        return code == 0 ? helper::kExitClean : helper::kExitUsage;
    }

    if (listDevices) {
        for (const auto& d : EglContext::listDevices())
            std::cout << d.index << "\t" << (d.drmNode.empty() ? "-" : d.drmNode)
                      << "\t" << (d.renderer.empty() ? "(unusable)" : d.renderer)
                      << "\n";
        return helper::kExitClean;
    }
    if (wallpaper.empty() || !fs::exists(fs::path(wallpaper) / "project.json")) {
        std::cerr << "desktop-canvas-scene: no project.json in '" << wallpaper
                  << "'\n";
        return helper::kExitUsage;
    }

    auto unsupported = unsupportedFeatures(wallpaper);
    if (check) {
        nlohmann::json report = {{"wallpaper", wallpaper},
                                 {"supported", unsupported.empty()},
                                 {"unsupported", unsupported}};
        std::cout << report.dump(2) << "\n";
        return unsupported.empty() ? helper::kExitClean
                                   : helper::kExitUnsupported;
    }

    EventLoop loop;
    std::unique_ptr<ControlChannel> channel;
    if (controlFd >= 0) channel = std::make_unique<ControlChannel>(loop, controlFd);

    if (strict && !unsupported.empty()) {
        log::info() << "unsupported content, leaving it to the fallback";
        if (channel) {
            channel->send(helper::encode({{"type", "hello"},
                                          {"proto", helper::kProtocolVersion},
                                          {"pid", static_cast<int>(getpid())}}));
            channel->send(helper::encode(
                {{"type", "unsupported"}, {"features", unsupported}}));
        }
        return helper::kExitUnsupported;
    }

    scenehost::PlaceholderRenderer renderer(kPlaceholder[0], kPlaceholder[1],
                                            kPlaceholder[2]);
    renderer.setMuted(muted);
    renderer.setVolume(volume);
    for (const auto& p : properties) {
        auto eq = p.find('=');
        if (eq != std::string::npos)
            renderer.setProperty(p.substr(0, eq), p.substr(eq + 1));
    }

    if (!screenshot.empty()) {
        int width = 0;
        int height = 0;
        if (!parseSize(size, width, height)) {
            std::cerr << "desktop-canvas-scene: bad --size '" << size << "'\n";
            return helper::kExitUsage;
        }
        EglContext egl;
        if (!egl.initDevice(device, glOptions(glDebug))) return 1;
        fs::path out = fs::absolute(screenshot);
        if (!noSandbox) scenehost::applySandbox(writablePaths(out.parent_path()));
        return scenehost::renderToPng(renderer, width, height, frames, fps,
                                      out.string())
                   ? helper::kExitClean
                   : 1;
    }

    std::unique_ptr<Platform> platform;
    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    const char* x11Display = std::getenv("DISPLAY");
#ifdef CANVAS_HAVE_WAYLAND
    if (!platform && waylandDisplay && *waylandDisplay) {
        auto p = std::make_unique<WaylandPlatform>(glOptions(glDebug));
        if (p->init(loop)) platform = std::move(p);
    }
#endif
#ifdef CANVAS_HAVE_X11
    if (!platform && x11Display && *x11Display) {
        auto p = std::make_unique<X11Platform>(glOptions(glDebug), fps);
        if (p->init(loop)) platform = std::move(p);
    }
#endif
    if (!platform) {
        log::error() << "no usable display (WAYLAND_DISPLAY="
                     << (waylandDisplay ? waylandDisplay : "") << " DISPLAY="
                     << (x11Display ? x11Display : "") << ")";
        return 1;
    }
    if (!noSandbox) scenehost::applySandbox(writablePaths(""));

    scenehost::SceneHostOptions options;
    options.outputs = outputs;
    options.layer = layer;
    options.fps = fps;
    options.span = span;
    scenehost::SceneHost host(loop, *platform, renderer, options);
    if (channel) host.attachChannel(std::move(channel));
    loop.onTerminate([&] { loop.quit(helper::kExitClean); });
    host.start();
    return loop.run();
}
