// Builds the argv for a linux-wallpaperengine scene process.
//
// Flag set verified against the installed linux-wallpaperengine build:
//   --screen-root <name> --bg <id>   per output rendering (Wayland and X11)
//   --window WxHxXxY                 windowed rendering (X11 reparent path)
//   --silent / --volume N            audio policy
//   --fps N, --scaling <mode>, --assets-dir <dir>, --disable-mouse
// If upstream changes flags, probeSupportedFlags() (which runs --help) lets
// the daemon detect skew at startup and log a clear error.
#pragma once

#include <string>
#include <vector>

namespace canvas {

struct LweOptions {
    std::string enginePath;   // binary path or "linux-wallpaperengine"
    std::string screenRoot;   // output connector name; empty for window mode
    std::string windowGeometry;  // "XxYxWxH" when screenRoot is empty
    std::string wallpaperDir;    // absolute path of the workshop item
    std::string assetsDir;       // empty = let lwe autodetect
    int fps = 30;
    std::string scaling = "fill";  // stretch | fit | fill | default
    bool muted = true;
    int volume = 15;
    bool disableMouse = false;
};

class LweCommandBuilder {
public:
    // Returns the full argv (argv[0] = engine path).
    static std::vector<std::string> build(const LweOptions& opts);

    // Runs "<engine> --help" and returns its stdout+stderr, empty on failure.
    // Used at startup to verify the flags above still exist.
    static std::string probeHelp(const std::string& enginePath);
};

}  // namespace canvas
