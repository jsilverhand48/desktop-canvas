// Value type describing one workshop wallpaper item, produced by
// WorkshopScanner from a <workshop id>/project.json directory.
#pragma once

#include <filesystem>
#include <string>

namespace canvas {

enum class WallpaperType {
    Scene,        // compiled scene.pkg, rendered by linux-wallpaperengine
    Video,        // video file, rendered in process via libmpv
    Web,          // HTML/JS, rendered by the optional web backend
    Unsupported,  // presets, malformed items, unknown types
};

inline const char* toString(WallpaperType t) {
    switch (t) {
        case WallpaperType::Scene: return "scene";
        case WallpaperType::Video: return "video";
        case WallpaperType::Web: return "web";
        case WallpaperType::Unsupported: return "unsupported";
    }
    return "unsupported";
}

struct WallpaperInfo {
    std::string id;               // workshop directory name, e.g. "1049326028"
    std::filesystem::path dir;    // absolute directory of the item
    WallpaperType type = WallpaperType::Unsupported;
    std::string title;
    std::string file;             // "file" from project.json, relative to dir
    std::string preview;          // preview image name, may be empty
    std::string unsupportedReason;  // set when type == Unsupported

    // Absolute path of the payload file (scene.pkg, mp4, index.html).
    std::filesystem::path filePath() const { return dir / file; }
};

}  // namespace canvas
