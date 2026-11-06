// Implementation of MediaInfo declared in MediaInfo.h.
#include "scene/audio/MediaInfo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace canvas::scene {

namespace {

std::string text(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>().substr(0, 1024) : std::string();
}

double number(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? std::max(0.0, it->get<double>()) : 0.0;
}

double linear(double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

double luminance(const Vec3& c) {
    return 0.2126 * linear(c[0]) + 0.7152 * linear(c[1]) + 0.0722 * linear(c[2]);
}

double distance2(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    double d = 0;
    for (size_t k = 0; k < 3; k++) d += (a[k] - b[k]) * (a[k] - b[k]);
    return d;
}

}  // namespace

MediaInfo MediaInfo::fromJson(const nlohmann::json& j) {
    MediaInfo m;
    if (!j.is_object()) return m;
    m.enabled = j.value("enabled", false);
    if (auto it = j.find("state"); it != j.end() && it->is_number_integer())
        m.state = std::clamp(it->get<int>(), 0, 2);
    m.title = text(j, "title");
    m.artist = text(j, "artist");
    m.subTitle = text(j, "subTitle");
    m.albumTitle = text(j, "albumTitle");
    m.albumArtist = text(j, "albumArtist");
    m.genres = text(j, "genres");
    m.contentType = text(j, "contentType");
    m.artPath = text(j, "thumbnail");
    m.position = number(j, "position");
    m.duration = number(j, "duration");
    if (auto it = j.find("timeline"); it != j.end() && it->is_number_integer() && it->get<int64_t>() >= 0)
        m.timelineSerial = uint64_t(it->get<int64_t>());
    return m;
}

bool MediaInfo::sameProperties(const MediaInfo& o) const {
    return title == o.title && artist == o.artist && subTitle == o.subTitle && albumTitle == o.albumTitle &&
           albumArtist == o.albumArtist && genres == o.genres && contentType == o.contentType;
}

double contrastRatio(const Vec3& a, const Vec3& b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

AlbumArtColors albumArtColors(const DecodedImage& image) {
    AlbumArtColors out;
    if (image.layout != PixelLayout::Rgba8 || image.width == 0 || image.height == 0 ||
        image.pixels.size() < size_t(image.width) * image.height * 4)
        return out;
    // Reduce to at most 64 x 64 samples (nearest), skipping clear pixels.
    std::vector<std::array<double, 3>> px;
    const uint32_t sw = std::min<uint32_t>(64, image.width), sh = std::min<uint32_t>(64, image.height);
    px.reserve(size_t(sw) * sh);
    for (uint32_t y = 0; y < sh; y++)
        for (uint32_t x = 0; x < sw; x++) {
            const size_t ix = size_t(x) * image.width / sw, iy = size_t(y) * image.height / sh;
            const uint8_t* p = &image.pixels[(iy * image.width + ix) * 4];
            if (p[3] < 128) continue;
            px.push_back({p[0] / 255.0, p[1] / 255.0, p[2] / 255.0});
        }
    if (px.empty()) return out;
    // k-means, k = 3, seeded deterministically: the first sample, then the
    // sample farthest from the seeds so far.
    std::array<std::array<double, 3>, 3> centre{};
    centre[0] = px[0];
    for (size_t c = 1; c < 3; c++) {
        double best = -1;
        for (const auto& p : px) {
            double d = 1e9;
            for (size_t k = 0; k < c; k++) d = std::min(d, distance2(p, centre[k]));
            if (d > best) {
                best = d;
                centre[c] = p;
            }
        }
    }
    std::array<size_t, 3> count{};
    for (int iteration = 0; iteration < 12; iteration++) {
        std::array<std::array<double, 3>, 3> sum{};
        count = {};
        for (const auto& p : px) {
            size_t nearest = 0;
            for (size_t k = 1; k < 3; k++)
                if (distance2(p, centre[k]) < distance2(p, centre[nearest])) nearest = k;
            for (size_t k = 0; k < 3; k++) sum[nearest][k] += p[k];
            count[nearest]++;
        }
        for (size_t c = 0; c < 3; c++)
            if (count[c])
                for (size_t k = 0; k < 3; k++) centre[c][k] = sum[c][k] / double(count[c]);
    }
    std::array<size_t, 3> order{0, 1, 2};
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return count[a] > count[b]; });
    auto vec = [&](size_t c) {
        return Vec3{float(centre[c][0]), float(centre[c][1]), float(centre[c][2])};
    };
    out.hasThumbnail = true;
    out.primary = vec(order[0]);
    out.secondary = count[order[1]] ? vec(order[1]) : out.primary;
    out.tertiary = count[order[2]] ? vec(order[2]) : out.secondary;
    const Vec3 black{0, 0, 0}, white{1, 1, 1};
    out.highContrast = contrastRatio(out.primary, black) >= contrastRatio(out.primary, white) ? black : white;
    if (contrastRatio(out.secondary, out.primary) >= 4.5) out.text = out.secondary;
    else if (contrastRatio(out.tertiary, out.primary) >= 4.5) out.text = out.tertiary;
    else out.text = out.highContrast;
    return out;
}

}  // namespace canvas::scene
