// The capability report of src/scene/model/README.md section 9: which
// features a loaded scene uses, which of them this build renders, and the
// JSON `desktop-canvas-scene --check` prints.
//
// Feature names are stable (the daemon caches them in its native support
// verdicts): scene, image, effects, solid-layer, fullscreen-layer,
// composition-layer, shape-layer, group, particles, text, sound, light,
// puppet, camera-path, 3d-model, script, timeline, user-properties,
// parallax, camera-shake, bloom, hdr, video-texture, sprite-animation,
// particle-controlpoints, particle-children, particle-trails (particle
// systems that use what phase P5b brings).
// "scene" (camera, clear colour, projection) is used by every scene.
#pragma once

#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "scene/model/SceneLoader.h"

namespace canvas::scene {

std::set<std::string> sceneFeatures(LoadedScene& scene);

// What this build renders; grows phase by phase (P4a: scene, image,
// effects, solid-layer, fullscreen-layer, composition-layer, group,
// user-properties, bloom, hdr; P4b: timeline, parallax, camera-shake,
// sprite-animation, video-texture, the last only with libmpv, which the
// helper's SceneCheck checks; P5a: particles; P5b: particle-controlpoints,
// particle-children, particle-trails; P6b: text).
const std::set<std::string>& supportedFeatures();

std::vector<std::string> unsupportedOf(const std::set<std::string>& features);

// The --check object: wallpaper, supported, features, unsupported, errors,
// warnings, dropped_warnings. loaded false means project.json or the scene
// file failed: the "unreadable" feature is reported unsupported.
nlohmann::json checkReport(const std::string& wallpaper, bool loaded,
                           const std::set<std::string>& features, const LoadReport& report);

}  // namespace canvas::scene
