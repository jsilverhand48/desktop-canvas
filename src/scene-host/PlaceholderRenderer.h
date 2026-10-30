// Stand-in Renderer until the native scene engine exists: clears every
// frame to one solid colour. It proves the helper's whole path (surfaces,
// pacing, protocol, screenshots) end to end and is what "native" mode shows
// for now. GL calls go through libepoxy.
#pragma once

#include "scene-host/Renderer.h"

namespace canvas::scenehost {

class PlaceholderRenderer : public Renderer {
public:
    // Colour components in 0..1.
    PlaceholderRenderer(float r, float g, float b) : r_(r), g_(g), b_(b) {}

    void advance(double /*dt*/) override {}
    void render(int width, int height) override;

private:
    float r_, g_, b_;
};

}  // namespace canvas::scenehost
