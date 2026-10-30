// Builds the argv for a linux-wallpaperengine scene process.
//
// Flag set verified against the installed linux-wallpaperengine build:
//   --screen-root <name> --bg <id>   per output rendering (Wayland and X11);
//                                    repeated per output so one process
//                                    can drive several screens
//   --window WxHxXxY                 windowed rendering (X11 reparent path)
//   --silent / --volume N            audio policy
//   --scaling <mode>                 per screen, applies to the preceding
//                                    --screen-root or --window
//   --fps N, --assets-dir <dir>, --disable-mouse
//   --layer <name>                   Wayland layer shell layer, once per
//                                    process (lwe r611+). lwe defaults to
//                                    "bottom" since r598, which KWin stacks
//                                    above the Plasma desktop icons, so the
//                                    daemon passes its own layer instead.
//   --set-property <name>=<value>    user property override, repeated per
//                                    LweOptions::properties entry
// If upstream changes flags, probeSupportedFlags() (which runs --help) lets
// the daemon detect skew at startup and log a clear error.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace canvas {

struct LweOptions {
    std::string enginePath;   // binary path or "linux-wallpaperengine"
    // Output connector names, all showing wallpaperDir in one process.
    // Empty for window mode.
    std::vector<std::string> screenRoots;
    std::string windowGeometry;  // "XxYxWxH" when screenRoots is empty
    std::string wallpaperDir;    // absolute path of the workshop item
    std::string assetsDir;       // empty = let lwe autodetect
    int fps = 30;
    std::string scaling = "fill";  // stretch | fit | fill | default
    bool muted = true;
    int volume = 15;
    bool disableMouse = false;
    // Layer shell layer for screen-root mode ("background" | "bottom").
    // Empty = omit --layer (window mode, or an lwe without the flag).
    std::string layer;
    // User property overrides, each emitted as `--set-property name=value`
    // (e.g. random scene variants from source/SceneOptions).
    std::vector<std::pair<std::string, std::string>> properties;
};

class LweCommandBuilder {
public:
    // Returns the full argv (argv[0] = engine path).
    static std::vector<std::string> build(const LweOptions& opts);

    // Runs "<engine> --help" and returns its stdout+stderr, empty on failure.
    // Used at startup to verify the flags above still exist.
    static std::string probeHelp(const std::string& enginePath);

    // True when probeHelp output lists --layer.
    static bool supportsLayer(const std::string& help);
};

}  // namespace canvas
