// Implementation of parseJson declared in Json.h.
#include "scene/model/Json.h"

namespace canvas::scene {

int jsonNesting(std::span<const uint8_t> bytes, int limit) {
    int depth = 0;
    int deepest = 0;
    bool inString = false;
    bool escaped = false;
    for (uint8_t c : bytes) {
        if (inString) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            if (++depth > deepest) deepest = depth;
            if (deepest > limit) return deepest;
        } else if (c == '}' || c == ']') {
            depth--;
        }
    }
    return deepest;
}

std::optional<nlohmann::json> parseJson(std::span<const uint8_t> bytes, std::string& error) {
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        bytes = bytes.subspan(3);
    if (bytes.size() > kMaxJsonBytes) {
        error = "larger than 64 MiB";
        return std::nullopt;
    }
    if (jsonNesting(bytes, kMaxJsonDepth) > kMaxJsonDepth) {
        error = "nested deeper than 64 levels";
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(bytes.begin(), bytes.end());
    } catch (const nlohmann::json::exception& e) {
        error = e.what();
        return std::nullopt;
    }
}

}  // namespace canvas::scene
