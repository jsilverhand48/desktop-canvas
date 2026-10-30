// Implementation of the headless screenshot path declared in Screenshot.h.
//
// GL reads rows bottom up, so the buffer is flipped before writing. The PNG
// is written to "<path>.tmp" and renamed into place, so a reader never sees
// a half written file.
#include "scene-host/Screenshot.h"

#include <epoxy/gl.h>
#include <png.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "core/Log.h"
#include "scene-host/Renderer.h"

namespace canvas::scenehost {

bool writePng(const std::string& path, int width, int height,
              const std::vector<uint8_t>& pixels, int channels) {
    int colorType = channels == 1   ? PNG_COLOR_TYPE_GRAY
                    : channels == 3 ? PNG_COLOR_TYPE_RGB
                    : channels == 4 ? PNG_COLOR_TYPE_RGBA
                                    : -1;
    if (width <= 0 || height <= 0 || colorType < 0 ||
        pixels.size() != static_cast<size_t>(width) * height * channels)
        return false;
    std::string tmp = path + ".tmp";
    FILE* file = std::fopen(tmp.c_str(), "wb");
    if (!file) {
        log::error() << "cannot write " << tmp;
        return false;
    }
    png_structp png =
        png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, info ? &info : nullptr);
        std::fclose(file);
        std::remove(tmp.c_str());
        log::error() << "libpng failed writing " << path;
        return false;
    }
    png_init_io(png, file);
    png_set_IHDR(png, info, static_cast<png_uint_32>(width),
                 static_cast<png_uint_32>(height), 8, colorType,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    const size_t stride = static_cast<size_t>(width) * channels;
    for (int y = 0; y < height; y++)
        png_write_row(png, const_cast<png_bytep>(pixels.data() + y * stride));
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    if (std::fclose(file) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        log::error() << "cannot move " << tmp << " to " << path << ": "
                     << ec.message();
        return false;
    }
    return true;
}

bool renderToPng(Renderer& renderer, int width, int height, int frames,
                 int fps, const std::string& path) {
    GLuint fbo = 0;
    GLuint color = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, color);
    bool complete =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    std::vector<uint8_t> rgba;
    if (complete) {
        double dt = 1.0 / (fps > 0 ? fps : 30);
        for (int i = 0; i < (frames > 0 ? frames : 1); i++) {
            renderer.advance(dt);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            renderer.render(width, height);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFinish();
        std::vector<uint8_t> raw(static_cast<size_t>(width) * height * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
        rgba.resize(raw.size());
        size_t row = static_cast<size_t>(width) * 4;
        for (int y = 0; y < height; y++)
            std::copy_n(raw.data() + static_cast<size_t>(height - 1 - y) * row,
                        row, rgba.data() + static_cast<size_t>(y) * row);
    } else {
        log::error() << "screenshot framebuffer incomplete";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteRenderbuffers(1, &color);
    glDeleteFramebuffers(1, &fbo);
    return complete && writePng(path, width, height, rgba);
}

}  // namespace canvas::scenehost
