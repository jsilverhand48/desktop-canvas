// Text measuring, line breaking and rasterizing, per
// src/scene/text/README.md sections 3, 4 and 6.
//
// layoutText() measures a text at `pointsize` * 300/72 pixels per em:
// lines broken at newlines and, with a width limit, at spaces; rows
// limited, the last kept row ending in "..." when asked; each character
// placed at whole pixel advances; the box is the widest line by the rows'
// height plus the padding on every side, in scene units (layout pixels).
// rasterizeText() draws that layout into an 8 bit coverage image of the
// box at a raster scale (raster pixels per layout pixel), rows top first.
// textRasterScale() picks that scale (section 6). No GPU.
#pragma once

#include <string>
#include <vector>

#include "scene/model/Field.h"
#include "scene/text/TextFont.h"

namespace canvas::scene {

struct TextStyle {
    std::string text;  // UTF-8
    float pointSize = 32;
    Vec2 padding{0, 0};
    std::string horizontalAlign = "center";  // left, center, right
    bool limitRows = false, limitWidth = false, ellipsis = false;
    int maxRows = 1;
    float maxWidth = 500;  // layout pixels
};

struct TextLine {
    std::u32string chars;
    std::vector<int> x;  // each character's pen position from the line start
    int width = 0;       // sum of the advances
    float offset = 0;    // the line's left edge in the box (alignment, padding)
};

struct TextLayout {
    Vec2 box{1, 1};  // scene units, padding included
    float pixelSize = 0, lineHeight = 0, ascender = 0;
    Vec2 padding{0, 0};
    std::vector<TextLine> lines;
};

TextLayout layoutText(TextFont& font, const TextStyle& style);

struct CoverageImage {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;  // width * height, rows top first
};

CoverageImage rasterizeText(TextFont& font, const TextLayout& layout, float scale);

// Raster pixels per layout pixel (section 6): the layer's mean scale times
// the frame's pixels per scene unit when above 1, keeping the raster at
// 1..1024 pixels per em.
float textRasterScale(float pixelSize, float meanScale, float framePixelsPerUnit);

// UTF-8 to code points; invalid bytes become U+FFFD.
std::u32string decodeUtf8(const std::string& text);

}  // namespace canvas::scene
