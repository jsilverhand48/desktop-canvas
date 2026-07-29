// Scans Wallpaper Engine workshop content directories.
//
// A content root is a directory shaped like
//   .../steamapps/workshop/content/431960
// holding one subdirectory per workshop item, each with a project.json.
// The scanner is tolerant by design: malformed JSON, missing files, or
// unknown types produce Unsupported entries with a reason instead of errors,
// because the daemon must keep running whatever the library contains.
//
// The scanner also derives the Wallpaper Engine assets directory (shipped
// with the Windows app under steamapps/common/wallpaper_engine/assets) which
// linux-wallpaperengine needs to render scenes.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "source/WallpaperInfo.h"

namespace canvas {

class WorkshopScanner {
public:
    // Scans every root and returns items sorted by id. Roots that do not
    // exist (e.g. removable media not mounted) are skipped silently.
    static std::vector<WallpaperInfo> scan(
        const std::vector<std::filesystem::path>& roots);

    // Parses one item directory. Returns nullopt only when project.json is
    // absent; any parse problem still yields an Unsupported entry.
    static std::optional<WallpaperInfo> scanItem(
        const std::filesystem::path& itemDir);

    // Well known Steam library locations that contain workshop content for
    // app 431960: ~/.local/share/Steam, ~/.steam/steam, and any
    // /run/media/<user>/*/SteamLibrary mount. Only existing paths are
    // returned.
    static std::vector<std::filesystem::path> defaultRoots();

    // Given a content root, walks up to steamapps and looks for
    // common/wallpaper_engine/assets in the same Steam library.
    static std::optional<std::filesystem::path> findAssetsDir(
        const std::vector<std::filesystem::path>& roots);
};

}  // namespace canvas
