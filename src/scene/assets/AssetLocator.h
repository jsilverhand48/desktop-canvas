// The file lookup of src/scene/assets/README.md section 2 for one
// wallpaper.
//
// find(path) normalizes the path (backslashes become slashes, empty and "."
// segments are dropped, ".." segments and absolute paths are refused) and
// returns the first match of: the compatibility overrides of section 2.1,
// the item's loose files, its scene.pkg, the Wallpaper Engine assets
// folder. Loose, override and assets files are opened with
// MappedFile::openBeneath, so no lookup leaves its layer's root. Returned
// bytes stay valid while the Asset lives (mapped files) and while the
// locator lives (package entries). find() is const and keeps no state, so
// it may run on several threads once open() has returned.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "scene/assets/MappedFile.h"
#include "scene/assets/PackageArchive.h"

namespace canvas::scene {

class AssetLocator {
public:
    enum class Layer { Compat, Loose, Package, Assets };
    static const char* layerName(Layer layer);

    struct Asset {
        std::span<const uint8_t> bytes;
        Layer layer = Layer::Package;
        std::shared_ptr<const MappedFile> mapping;  // null for package entries
    };

    // itemDir: the wallpaper folder. assetsDir: the Wallpaper Engine assets
    // folder; empty means no assets layer and no overrides. workshopId: the
    // id compared with an override's maximumprojectid; empty means the
    // folder name. False (with error()) when itemDir is not a folder or its
    // scene.pkg is invalid; a missing assets folder is not an error.
    bool open(const std::filesystem::path& itemDir,
              const std::filesystem::path& assetsDir = {},
              std::string workshopId = {});

    std::optional<Asset> find(std::string_view path) const;

    // nullptr when the item has no scene.pkg.
    const PackageArchive* package() const { return hasPackage_ ? &package_ : nullptr; }
    const std::filesystem::path& itemDir() const { return itemDir_; }
    const std::filesystem::path& assetsDir() const { return assetsDir_; }
    // Override folders that apply to this wallpaper (for reports).
    std::vector<std::filesystem::path> activeOverrides() const;
    const std::string& error() const { return error_; }

    // Section 2 normalization; nullopt when the path is refused.
    static std::optional<std::string> normalize(std::string_view path);
    // <library>/steamapps/workshop/content/431960/<id> gives
    // <library>/steamapps/common/wallpaper_engine/assets when that folder
    // exists (the same Steam library rule as WorkshopScanner::findAssetsDir).
    static std::optional<std::filesystem::path> guessAssetsDir(
        const std::filesystem::path& itemDir);

private:
    struct Override {
        std::string prefix;              // "shaders/workshop/<effect id>/"
        std::vector<std::string> files;  // the configured frag and vert names
        std::filesystem::path dir;
    };

    void loadOverrides();
    std::optional<Asset> mapFile(const std::filesystem::path& root,
                                 const std::string& relative, Layer layer) const;

    std::filesystem::path itemDir_;
    std::filesystem::path assetsDir_;
    std::string workshopId_;
    PackageArchive package_;
    bool hasPackage_ = false;
    std::vector<Override> overrides_;
    std::string error_;
};

}  // namespace canvas::scene
