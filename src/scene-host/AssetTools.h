// Asset inspection modes of desktop-canvas-scene, per
// src/scene/assets/README.md section 4.1.
//
// extractAsset() looks an entry up through the wallpaper's lookup layers.
// A .tex file is decoded (first image, first mip, DXT expanded on the CPU)
// and written as a PNG: R8 as grayscale, RG88 as RGB with blue 0 (what a
// shader samples), everything else as RGBA; volume slices stay stacked
// vertically. Its header goes to stdout as JSON. Any other file is copied
// as is. dumpPackage() writes every scene.pkg entry below a folder that
// must be empty or absent, refusing to replace anything. Both return a
// helper exit code: 0 done, 1 failure, 2 usage.
#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "scene/assets/AssetLocator.h"
#include "scene/assets/TextureFile.h"

namespace canvas::scenehost {

int extractAsset(const scene::AssetLocator& assets, const std::string& entry,
                 const std::filesystem::path& out);
int dumpPackage(const scene::AssetLocator& assets, const std::filesystem::path& dir);

// The header fields of a parsed texture, for --extract and reports.
nlohmann::json describeTexture(const scene::TextureFile& texture);

}  // namespace canvas::scenehost
