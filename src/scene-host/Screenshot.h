// Headless rendering to a PNG file.
//
// renderToPng() needs a current GL 3.3 context (EglContext::initDevice
// gives a surfaceless one). It renders `frames` ticks of the renderer into
// an RGBA8 framebuffer object of the requested size, advancing 1/fps per
// tick, reads the last frame back and writes it top row first as an 8 bit
// RGBA PNG through libpng. writePng() is the file writer on its own.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace canvas::scenehost {

class Renderer;

bool renderToPng(Renderer& renderer, int width, int height, int frames,
                 int fps, const std::string& path);

// rgba: width * height * 4 bytes, rows top to bottom.
bool writePng(const std::string& path, int width, int height,
              const std::vector<uint8_t>& rgba);

}  // namespace canvas::scenehost
