// Implementation of Playlist declared in Playlist.h. See the header for the
// file format.
#include "source/Playlist.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "core/Log.h"

namespace canvas {

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    auto begin = std::find_if_not(s.begin(), s.end(), isSpace);
    auto end = std::find_if_not(s.rbegin(), s.rend(), isSpace).base();
    return begin < end ? std::string(begin, end) : std::string();
}

}  // namespace

bool Playlist::load(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        log::warn() << "playlist " << path << " cannot be read";
        return false;
    }
    std::vector<std::string> ids;
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::string id = trim(line);
        if (id.empty()) continue;
        if (std::find(ids.begin(), ids.end(), id) == ids.end())
            ids.push_back(id);
    }
    ids_ = std::move(ids);
    path_ = path;
    log::info() << "playlist " << path << ": " << ids_.size() << " id(s)";
    return true;
}

void Playlist::clear() {
    ids_.clear();
    path_.clear();
}

}  // namespace canvas
