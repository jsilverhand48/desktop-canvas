// Implementation of TextFont and systemFontFile declared in TextFont.h.
#include "scene/text/TextFont.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cmath>

#ifdef CANVAS_SCENE_HAVE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

namespace canvas::scene {

std::shared_ptr<TextFont> TextFont::load(std::span<const uint8_t> bytes, std::shared_ptr<const void> keep,
                                         std::string& error) {
    std::shared_ptr<TextFont> font(new TextFont());
    if (FT_Init_FreeType(&font->library_)) {
        error = "FreeType could not start";
        return nullptr;
    }
    if (FT_New_Memory_Face(font->library_, bytes.data(), FT_Long(bytes.size()), 0, &font->face_)) {
        error = "not a font FreeType reads";
        return nullptr;
    }
    if (!FT_IS_SCALABLE(font->face_)) {
        error = "not a scalable font";
        return nullptr;
    }
    FT_Select_Charmap(font->face_, FT_ENCODING_UNICODE);
    font->keep_ = std::move(keep);
    font->setPixelSize(32);
    return font;
}

TextFont::~TextFont() {
    if (face_) FT_Done_Face(face_);
    if (library_) FT_Done_FreeType(library_);
}

void TextFont::setPixelSize(float pixels) {
    if (pixels == pixels_) return;
    pixels_ = pixels;
    FT_Set_Char_Size(face_, 0, FT_F26Dot6(std::lround(double(pixels) * 64)), 72, 72);
}

float TextFont::lineHeight() const { return float(face_->size->metrics.height) / 64.0f; }

float TextFont::ascender() const { return float(face_->size->metrics.ascender) / 64.0f; }

int TextFont::advance(char32_t c) {
    const FT_UInt index = FT_Get_Char_Index(face_, FT_ULong(c));
    if (FT_Load_Glyph(face_, index, FT_LOAD_DEFAULT)) return 0;
    return int(std::floor(double(face_->glyph->linearHoriAdvance) / 65536.0));
}

bool TextFont::glyph(char32_t c, Glyph& out) {
    const FT_UInt index = FT_Get_Char_Index(face_, FT_ULong(c));
    if (FT_Load_Glyph(face_, index, FT_LOAD_DEFAULT) || FT_Render_Glyph(face_->glyph, FT_RENDER_MODE_NORMAL))
        return false;
    const FT_Bitmap& b = face_->glyph->bitmap;
    if (b.pixel_mode != FT_PIXEL_MODE_GRAY) return false;
    out.left = face_->glyph->bitmap_left;
    out.top = face_->glyph->bitmap_top;
    out.width = int(b.width);
    out.rows = int(b.rows);
    out.pitch = b.pitch;
    out.pixels = b.buffer;
    return true;
}

std::string systemFontFile(const std::string& family) {
#ifdef CANVAS_SCENE_HAVE_FONTCONFIG
    FcConfig* config = FcInitLoadConfigAndFonts();
    if (!config) return {};
    std::string file;
    FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>(family.c_str()));
    if (pattern) {
        FcConfigSubstitute(config, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult result = FcResultNoMatch;
        FcPattern* match = FcFontMatch(config, pattern, &result);
        FcChar8* path = nullptr;
        if (match && FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch && path)
            file = reinterpret_cast<const char*>(path);
        if (match) FcPatternDestroy(match);
        FcPatternDestroy(pattern);
    }
    FcConfigDestroy(config);
    return file;
#else
    (void)family;
    return {};
#endif
}

}  // namespace canvas::scene
