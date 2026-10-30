// Implementation of NativeSupportCache declared in NativeSupportCache.h.
#include "render/NativeSupportCache.h"

#include <sys/stat.h>

#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/Log.h"

namespace canvas {

namespace fs = std::filesystem;
using json = nlohmann::json;

NativeSupportCache::NativeSupportCache(fs::path file) : file_(std::move(file)) {}

fs::path NativeSupportCache::defaultPath() {
    fs::path base;
    if (const char* cache = std::getenv("XDG_CACHE_HOME"); cache && *cache)
        base = cache;
    else if (const char* home = std::getenv("HOME"); home && *home)
        base = fs::path(home) / ".cache";
    else
        base = fs::temp_directory_path();
    return base / "desktop-canvas" / "native-support.json";
}

std::string NativeSupportCache::stampOf(const fs::path& file) {
    struct stat st{};
    if (stat(file.c_str(), &st) != 0) return {};
    long long ns = static_cast<long long>(st.st_mtim.tv_sec) * 1000000000LL +
                   st.st_mtim.tv_nsec;
    return std::to_string(ns) + ":" + std::to_string(st.st_size);
}

void NativeSupportCache::load() {
    verdicts_.clear();
    std::ifstream in(file_);
    if (!in) return;
    json j = json::parse(in, nullptr, false);
    if (!j.is_object()) {
        log::warn() << "ignoring unreadable " << file_.string();
        return;
    }
    for (const auto& [id, v] : j.items()) {
        if (!v.is_object()) continue;
        Verdict verdict;
        verdict.reason = v.value("reason", "");
        verdict.contentStamp = v.value("content", "");
        verdict.helperStamp = v.value("helper", "");
        if (auto f = v.find("features"); f != v.end() && f->is_array())
            for (const auto& feature : *f)
                if (feature.is_string())
                    verdict.features.push_back(feature.get<std::string>());
        verdicts_[id] = std::move(verdict);
    }
}

const NativeSupportCache::Verdict* NativeSupportCache::find(
    const std::string& id, const std::string& contentStamp,
    const std::string& helperStamp) const {
    auto it = verdicts_.find(id);
    if (it == verdicts_.end()) return nullptr;
    if (it->second.contentStamp != contentStamp ||
        it->second.helperStamp != helperStamp)
        return nullptr;
    return &it->second;
}

void NativeSupportCache::record(const std::string& id, Verdict verdict) {
    verdicts_[id] = std::move(verdict);
    save();
}

void NativeSupportCache::forget(const std::string& id) {
    if (verdicts_.erase(id)) save();
}

bool NativeSupportCache::save() const {
    json j = json::object();
    for (const auto& [id, v] : verdicts_)
        j[id] = {{"reason", v.reason},
                 {"features", v.features},
                 {"content", v.contentStamp},
                 {"helper", v.helperStamp}};
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    fs::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            log::warn() << "cannot write " << tmp.string();
            return false;
        }
        out << j.dump(2) << "\n";
        if (!out) return false;
    }
    fs::rename(tmp, file_, ec);
    if (ec) {
        log::warn() << "cannot replace " << file_.string() << ": " << ec.message();
        return false;
    }
    return true;
}

}  // namespace canvas
