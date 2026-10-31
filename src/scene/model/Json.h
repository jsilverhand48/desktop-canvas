// JSON reading for scene files, per src/scene/model/README.md section 1.
//
// parseJson() skips a leading byte order mark, refuses input above
// kMaxJsonBytes or nested deeper than kMaxJsonDepth before parsing (a
// bracket count outside strings), and accepts only strict JSON (no
// comments, no NaN). On failure it returns nullopt with a short reason.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include <nlohmann/json.hpp>

namespace canvas::scene {

inline constexpr size_t kMaxJsonBytes = 64u << 20;
inline constexpr int kMaxJsonDepth = 64;

std::optional<nlohmann::json> parseJson(std::span<const uint8_t> bytes, std::string& error);

// Deepest bracket nesting outside strings; stops counting past limit.
int jsonNesting(std::span<const uint8_t> bytes, int limit);

}  // namespace canvas::scene
