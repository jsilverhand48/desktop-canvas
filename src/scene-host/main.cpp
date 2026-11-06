// desktop-canvas-scene: renders one Wallpaper Engine scene on one or more
// outputs. The daemon starts it (engine "native" or "auto", see
// render/InstanceManager.h) with --control-fd; it also runs by hand.
//
// Modes:
//   desktop-canvas-scene [options] WALLPAPER_DIR
//       show the scene on --output outputs (default all) as layer shell
//       surfaces (Wayland) or desktop windows (X11)
//   desktop-canvas-scene --window WxH [options] WALLPAPER_DIR
//       show it in an ordinary window instead (previews, debugging with the
//       pointer); closing the window ends the helper
//   desktop-canvas-scene --screenshot FILE [--size WxH] [--frames N]
//                        [--device NAME] WALLPAPER_DIR
//       headless: render on an EGL device and write a PNG
//   desktop-canvas-scene --check WALLPAPER_DIR
//       print the support report as JSON and exit (0 supported, 3 not);
//       see scene/model/README.md section 9
//   desktop-canvas-scene --check-all ROOT [--json]
//       check every scene folder in ROOT: a summary, or a JSON array
//   desktop-canvas-scene --list-devices
//       list EGL devices usable for --screenshot
//   desktop-canvas-scene --extract ENTRY OUT WALLPAPER_DIR
//       look ENTRY up like the renderer does (scene/assets/README.md
//       section 2); a .tex is decoded to a PNG at OUT, anything else copied
//   desktop-canvas-scene --dump DIR WALLPAPER_DIR
//       write every scene.pkg entry below DIR (empty or absent)
//
// Without --assets-dir the Wallpaper Engine assets folder is looked for in
// the Steam library holding the wallpaper (AssetLocator::guessAssetsDir).
//
// --strict (what "auto" passes) makes content that needs anything this
// build cannot render a reason to stop: the helper reports the list over
// the control channel and exits with helper::kExitUnsupported so the daemon
// falls back to linux-wallpaperengine. Without it ("native"), whatever is
// supported is shown and the rest skipped. The scene engine
// (NativeRenderer, scene/gl/SceneRenderer) draws image, solid, composition
// and fullscreen layers with their effects, their motion, particle and
// text layers (plan phases P4, P5 and P6b); a scene it cannot read or
// with nothing it can draw shows a solid placeholder colour. --scaling
// and --clamp say how the scene meets the output (scene/render/README.md
// section 1); --disable-particles leaves particle layers out (like
// --render-debug no-particles). Compiled shader programs are cached in
// $XDG_CACHE_HOME/desktop-canvas/shaders.
//
// Exit codes follow process/HelperProtocol.h: 0 clean, 2 usage, 3
// unsupported, 1 runtime failure (counted as a crash by the daemon).
#include <unistd.h>

#include <algorithm>
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
#include "scene-host/AssetTools.h"
#include "scene-host/NativeRenderer.h"
#include "scene-host/PlaceholderRenderer.h"
#include "scene-host/SceneCheck.h"
#include "scene-host/Sandbox.h"
#include "scene-host/SceneHost.h"
#include "scene-host/Screenshot.h"
#include "scene/assets/AssetLocator.h"

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

// $XDG_DATA_HOME/desktop-canvas/localstorage (or ~/.local/share/...),
// where scripts keep their localStorage (src/scene/script/README.md
// section 3); empty without either variable.
fs::path localStorageDir() {
    const char* data = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    if (data && *data) return fs::path(data) / "desktop-canvas" / "localstorage";
    if (home && *home) return fs::path(home) / ".local" / "share" / "desktop-canvas" / "localstorage";
    return {};
}

// $XDG_CACHE_HOME/desktop-canvas/shaders (or ~/.cache/...); empty without
// either variable.
fs::path shaderCacheDir() {
    const char* cache = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    if (cache && *cache) return fs::path(cache) / "desktop-canvas" / "shaders";
    if (home && *home) return fs::path(home) / ".cache" / "desktop-canvas" / "shaders";
    return {};
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
    std::vector<std::string> extract;
    std::string dump;
    std::string checkAllRoot;
    bool jsonOutput = false;
    std::vector<std::string> renderDebug;
    std::string window;

    app.add_option("wallpaper", wallpaper, "Wallpaper directory (has project.json)");
    app.add_option("--control-fd", controlFd, "Daemon control socket (HelperProtocol)");
    int audioFd = -1;
    app.add_option("--audio-fd", audioFd, "Daemon audio spectrum memory (src/scene/audio/README.md)");
    // Repeatable options take one value per use, so a WALLPAPER_DIR after
    // them stays the positional.
    app.add_option("--output", outputs, "Output to cover (repeatable; default all)")
        ->allow_extra_args(false);
    app.add_option("--layer", layer, "Layer shell layer: background | bottom");
    app.add_option("--fps", fps, "Frame rate cap")->check(CLI::Range(1, 240));
    app.add_option("--scaling", scaling, "fill | fit | stretch | default");
    app.add_option("--clamp", clamp, "clamp | border | repeat");
    app.add_option("--assets-dir", assetsDir, "Wallpaper Engine assets directory");
    app.add_option("--set-property", properties, "NAME=VALUE user property (repeatable)")
        ->allow_extra_args(false);
    app.add_flag("--muted", muted, "Start muted");
    app.add_option("--volume", volume, "Volume 0..100")->check(CLI::Range(0, 100));
    app.add_flag("--strict", strict, "Exit 3 when anything is unsupported");
    bool span = false;
    app.add_flag("--span", span, "Stretch one canvas over every --output");
    app.add_option("--window", window, "WxH: show in an ordinary window instead");
    bool disableParticles = false;
    app.add_flag("--disable-particles", disableParticles, "Leave particle layers out");
    app.add_flag("--check", check, "Print the support report and exit");
    app.add_option("--screenshot", screenshot, "Render headless into this PNG");
    app.add_option("--size", size, "Screenshot size WxH");
    app.add_option("--frames", frames, "Screenshot frames to render")->check(CLI::Range(1, 10000));
    app.add_option("--device", device, "EGL device for --screenshot (index or name)");
    app.add_flag("--list-devices", listDevices, "List EGL devices and exit");
    app.add_flag("--no-sandbox", noSandbox, "Do not apply the Landlock sandbox");
    app.add_flag("--gl-debug", glDebug, "Create a GL debug context");
    app.add_option("--extract", extract, "ENTRY OUT: decode or copy one file and exit")
        ->expected(2)
        ->allow_extra_args(false);  // leave WALLPAPER_DIR to the positional
    app.add_option("--dump", dump, "Write every scene.pkg entry below this folder");
    app.add_option("--check-all", checkAllRoot, "Check every scene folder in this root");
    app.add_flag("--json", jsonOutput, "With --check-all: print a JSON array");
    app.add_option("--render-debug", renderDebug,
                   "pass-log, base-only, no-bloom, ultra, no-solid-final, object=ID, skip-object=ID, "
                   "skip-effect=ID[:N], dump-programs=DIR (comma separated, repeatable)")
        ->allow_extra_args(false);
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
    if (!checkAllRoot.empty()) return scenehost::checkAll(checkAllRoot, assetsDir, jsonOutput);
    if (wallpaper.empty() || !fs::exists(fs::path(wallpaper) / "project.json")) {
        std::cerr << "desktop-canvas-scene: no project.json in '" << wallpaper
                  << "'\n";
        return helper::kExitUsage;
    }

    if (assetsDir.empty())
        if (auto guess = scene::AssetLocator::guessAssetsDir(wallpaper))
            assetsDir = guess->string();

    if (!extract.empty() || !dump.empty()) {
        scene::AssetLocator assets;
        if (!assets.open(wallpaper, assetsDir)) {
            std::cerr << "desktop-canvas-scene: " << assets.error() << "\n";
            return 1;
        }
        if (!extract.empty()) {
            fs::path out = fs::absolute(extract[1]);
            if (!noSandbox) scenehost::applySandbox(writablePaths(out.parent_path()));
            return scenehost::extractAsset(assets, extract[0], out);
        }
        fs::path out = fs::absolute(dump);
        std::error_code ec;
        fs::create_directories(out, ec);  // must exist to be sandbox writable
        if (!noSandbox) scenehost::applySandbox(writablePaths(out.string()));
        return scenehost::dumpPackage(assets, out);
    }

    scene::PropertyValues overrides;
    for (const auto& p : properties) {
        auto eq = p.find('=');
        if (eq != std::string::npos) overrides[p.substr(0, eq)] = p.substr(eq + 1);
    }
    scenehost::Examination exam = scenehost::examine(wallpaper, assetsDir, overrides);
    const std::vector<std::string>& unsupported = exam.unsupported;
    if (check) {
        std::cout << exam.report.dump(2) << "\n";
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

    scenehost::PlaceholderRenderer placeholder(kPlaceholder[0], kPlaceholder[1],
                                               kPlaceholder[2]);
    scenehost::NativeRenderer::Options nativeOptions;
    nativeOptions.scaling = scene::scalingFromName(scaling);
    nativeOptions.clamp = scene::clampFromName(clamp);
    nativeOptions.shaderCache = shaderCacheDir();
    nativeOptions.audioFd = audioFd;
    nativeOptions.sound = screenshot.empty();  // screenshots stay silent
    // Scripts keep localStorage in <dir>/<wallpaper folder name>.json, but
    // not for screenshots: one-off runs leave what the wallpaper remembers.
    fs::path storageDir;
    if (screenshot.empty()) {
        storageDir = localStorageDir();
        std::error_code ec;
        if (!storageDir.empty()) fs::create_directories(storageDir, ec);  // must exist to be sandbox writable
        if (!storageDir.empty() && !ec) {
            fs::path item = fs::absolute(wallpaper).lexically_normal();
            if (item.filename().empty()) item = item.parent_path();
            nativeOptions.scriptStorage = storageDir / (item.filename().string() + ".json");
        } else {
            storageDir.clear();
        }
    }
    std::copy_n(kPlaceholder, 3, nativeOptions.placeholder);
    nativeOptions.debug.noParticles = disableParticles;
    for (const auto& list : renderDebug) {
        std::string error;
        if (!nativeOptions.debug.parse(list, error)) {
            std::cerr << "desktop-canvas-scene: " << error << "\n";
            return helper::kExitUsage;
        }
    }
    std::string loadError;
    auto native = scenehost::NativeRenderer::create(wallpaper, assetsDir, overrides,
                                                    nativeOptions, loadError);
    if (!native) log::warn() << "scene not loaded, showing a placeholder: " << loadError;
    scenehost::Renderer& renderer =
        native ? static_cast<scenehost::Renderer&>(*native) : placeholder;
    renderer.setMuted(muted);
    renderer.setVolume(volume);
    for (const auto& [name, value] : overrides) renderer.setProperty(name, value);

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
        bool written = scenehost::renderToPng(renderer, width, height, frames, fps,
                                              out.string());
        if (native) {
            for (const auto& p : native->problems()) std::cerr << "problem: " << p << "\n";
            native->shutdown();  // while the context is still current
        }
        return written ? helper::kExitClean : 1;
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
    if (!noSandbox) scenehost::applySandbox(writablePaths(storageDir.string()));

    scenehost::SceneHostOptions options;
    if (!window.empty() && !parseSize(window, options.windowWidth, options.windowHeight)) {
        std::cerr << "desktop-canvas-scene: bad --window '" << window << "'\n";
        return helper::kExitUsage;
    }
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
