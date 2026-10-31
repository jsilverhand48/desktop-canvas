// What went wrong while loading a scene (src/scene/model/README.md
// sections 1 and 9): errors are files that could not be used, warnings are
// values that kept their default (type tolerance). Warnings stop being
// stored after kMaxWarnings and are only counted, so hostile files cannot
// grow the report without bound.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace canvas::scene {

struct LoadReport {
    static constexpr size_t kMaxWarnings = 200;

    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    size_t droppedWarnings = 0;

    void error(std::string text) { errors.push_back(std::move(text)); }
    void warn(std::string text) {
        if (warnings.size() < kMaxWarnings) warnings.push_back(std::move(text));
        else droppedWarnings++;
    }
};

}  // namespace canvas::scene
