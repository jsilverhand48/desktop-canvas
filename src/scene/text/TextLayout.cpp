// Implementation of the text layout declared in TextLayout.h; section
// numbers refer to src/scene/text/README.md.
#include "scene/text/TextLayout.h"

#include <algorithm>
#include <cmath>

namespace canvas::scene {

std::u32string decodeUtf8(const std::string& text) {
    std::u32string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        int n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + size_t(n) > text.size()) {
            out.push_back(0xFFFD);
            i++;
            continue;
        }
        char32_t cp = n == 1 ? c : c & (0xFF >> (n + 1));
        bool ok = true;
        for (int k = 1; k < n; k++) {
            const unsigned char b = static_cast<unsigned char>(text[i + size_t(k)]);
            if ((b & 0xC0) != 0x80) ok = false;
            cp = (cp << 6) | (b & 0x3F);
        }
        out.push_back(ok ? cp : char32_t(0xFFFD));
        i += ok ? size_t(n) : 1;
    }
    return out;
}

namespace {

TextLine makeLine(TextFont& font, std::u32string chars) {
    TextLine line;
    line.chars = std::move(chars);
    int pen = 0;
    for (char32_t c : line.chars) {
        line.x.push_back(pen);
        pen += font.advance(c);
    }
    line.width = pen;
    return line;
}

// Breaks one paragraph at spaces (or between characters when a word alone
// is too wide) so no line is wider than limit (section 4).
std::vector<std::u32string> wrap(TextFont& font, const std::u32string& text, float limit) {
    std::vector<std::u32string> out;
    std::u32string line;
    int width = 0;
    size_t lastSpace = std::u32string::npos;
    for (char32_t c : text) {
        const int a = font.advance(c);
        if (!line.empty() && float(width + a) > limit) {
            if (c == U' ') {  // the break falls on a space: drop it
                out.push_back(line);
                line.clear();
                width = 0;
                lastSpace = std::u32string::npos;
                continue;
            }
            if (lastSpace != std::u32string::npos) {
                std::u32string rest = line.substr(lastSpace + 1);
                line.resize(lastSpace);
                out.push_back(line);
                line = rest;
            } else {
                out.push_back(line);
                line.clear();
            }
            width = 0;
            for (char32_t r : line) width += font.advance(r);
            lastSpace = std::u32string::npos;
        }
        if (c == U' ') lastSpace = line.size();
        line.push_back(c);
        width += a;
    }
    out.push_back(line);
    return out;
}

}  // namespace

TextLayout layoutText(TextFont& font, const TextStyle& style) {
    TextLayout layout;
    layout.pixelSize = std::max(1.0f, style.pointSize * 300.0f / 72.0f);
    font.setPixelSize(layout.pixelSize);
    layout.lineHeight = font.lineHeight();
    layout.ascender = font.ascender();
    layout.padding = style.padding;

    // Paragraphs at newlines (carriage returns dropped), wrapped by width.
    std::vector<std::u32string> rows;
    std::u32string paragraph;
    const std::u32string text = decodeUtf8(style.text);
    auto flush = [&] {
        if (style.limitWidth && style.maxWidth > 0) {
            for (auto& r : wrap(font, paragraph, style.maxWidth)) rows.push_back(std::move(r));
        } else {
            rows.push_back(paragraph);
        }
        paragraph.clear();
    };
    for (char32_t c : text) {
        if (c == U'\r') continue;
        if (c == U'\n') {
            flush();
            continue;
        }
        paragraph.push_back(c);
    }
    flush();

    // Row limit, the last kept row ending in an ellipsis (section 4).
    if (style.limitRows && style.maxRows > 0 && rows.size() > size_t(style.maxRows)) {
        rows.resize(size_t(style.maxRows));
        if (style.ellipsis) {
            std::u32string& last = rows.back();
            int dots = 3 * font.advance(U'.');
            int width = 0;
            for (char32_t c : last) width += font.advance(c);
            const float limit = style.limitWidth ? style.maxWidth : float(width + dots);
            while (!last.empty() && float(width + dots) > limit) {
                width -= font.advance(last.back());
                last.pop_back();
            }
            last += U"...";
        }
    }

    int widest = 0;
    for (auto& r : rows) {
        layout.lines.push_back(makeLine(font, std::move(r)));
        widest = std::max(widest, layout.lines.back().width);
    }
    for (auto& line : layout.lines) {
        float slack = float(widest - line.width);
        float align = style.horizontalAlign == "left" ? 0.0f : style.horizontalAlign == "right" ? slack : slack / 2;
        line.offset = style.padding[0] + align;
    }
    layout.box = {std::max(1.0f, float(widest) + 2 * style.padding[0]),
                  std::max(1.0f, float(layout.lines.size()) * layout.lineHeight + 2 * style.padding[1])};
    return layout;
}

CoverageImage rasterizeText(TextFont& font, const TextLayout& layout, float scale) {
    CoverageImage image;
    image.width = std::max(1, int(std::ceil(layout.box[0] * scale)));
    image.height = std::max(1, int(std::ceil(layout.box[1] * scale)));
    image.pixels.assign(size_t(image.width) * size_t(image.height), 0);
    font.setPixelSize(layout.pixelSize * scale);
    for (size_t i = 0; i < layout.lines.size(); i++) {
        const TextLine& line = layout.lines[i];
        const float baseline = (layout.padding[1] + layout.ascender + float(i) * layout.lineHeight) * scale;
        for (size_t k = 0; k < line.chars.size(); k++) {
            TextFont::Glyph g;
            if (!font.glyph(line.chars[k], g) || !g.pixels) continue;
            const int x0 = int(std::lround((line.offset + float(line.x[k])) * scale)) + g.left;
            const int y0 = int(std::lround(baseline)) - g.top;
            for (int y = 0; y < g.rows; y++) {
                const int ty = y0 + y;
                if (ty < 0 || ty >= image.height) continue;
                const uint8_t* src = g.pixels + std::ptrdiff_t(y) * g.pitch;
                uint8_t* dst = image.pixels.data() + size_t(ty) * size_t(image.width);
                for (int x = 0; x < g.width; x++) {
                    const int tx = x0 + x;
                    if (tx >= 0 && tx < image.width) dst[tx] = std::max(dst[tx], src[x]);
                }
            }
        }
    }
    font.setPixelSize(layout.pixelSize);
    return image;
}

float textRasterScale(float pixelSize, float meanScale, float framePixelsPerUnit) {
    float scale = std::max(1e-3f, std::fabs(meanScale)) * std::max(1.0f, framePixelsPerUnit);
    const float em = std::clamp(std::round(pixelSize * scale), 1.0f, 1024.0f);
    return em / std::max(1.0f, pixelSize);
}

}  // namespace canvas::scene
