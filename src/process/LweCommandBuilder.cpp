// Implementation of LweCommandBuilder declared in LweCommandBuilder.h.
#include "process/LweCommandBuilder.h"

#include <array>
#include <cstdio>
#include <memory>

namespace canvas {

std::vector<std::string> LweCommandBuilder::build(const LweOptions& opts) {
    std::vector<std::string> argv;
    argv.push_back(opts.enginePath.empty() ? "linux-wallpaperengine"
                                           : opts.enginePath);
    if (!opts.screenRoots.empty()) {
        // Each screen gets its own --bg and --scaling; lwe binds both to
        // the preceding --screen-root.
        for (const auto& root : opts.screenRoots) {
            argv.push_back("--screen-root");
            argv.push_back(root);
            argv.push_back("--bg");
            argv.push_back(opts.wallpaperDir);
            argv.push_back("--scaling");
            argv.push_back(opts.scaling);
        }
    } else {
        if (!opts.windowGeometry.empty()) {
            argv.push_back("--window");
            argv.push_back(opts.windowGeometry);
        }
        argv.push_back("--scaling");
        argv.push_back(opts.scaling);
    }
    if (!opts.screenRoots.empty() && !opts.layer.empty()) {
        argv.push_back("--layer");
        argv.push_back(opts.layer);
    }
    argv.push_back("--fps");
    argv.push_back(std::to_string(opts.fps));
    if (opts.muted) {
        argv.push_back("--silent");
    } else {
        argv.push_back("--volume");
        argv.push_back(std::to_string(opts.volume));
    }
    if (!opts.assetsDir.empty()) {
        argv.push_back("--assets-dir");
        argv.push_back(opts.assetsDir);
    }
    if (opts.disableMouse) argv.push_back("--disable-mouse");
    for (const auto& [name, value] : opts.properties) {
        argv.push_back("--set-property");
        argv.push_back(name + "=" + value);
    }
    // Window mode takes the wallpaper as the positional default background.
    if (opts.screenRoots.empty()) argv.push_back(opts.wallpaperDir);
    return argv;
}

std::string LweCommandBuilder::probeHelp(const std::string& enginePath) {
    std::string cmd =
        (enginePath.empty() ? "linux-wallpaperengine" : enginePath) +
        " --help 2>&1";
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd.c_str(), "r"),
                                               pclose);
    if (!pipe) return "";
    std::string out;
    std::array<char, 4096> buf;
    while (fgets(buf.data(), buf.size(), pipe.get())) out += buf.data();
    return out;
}

bool LweCommandBuilder::supportsLayer(const std::string& help) {
    return help.find("--layer") != std::string::npos;
}

}  // namespace canvas
