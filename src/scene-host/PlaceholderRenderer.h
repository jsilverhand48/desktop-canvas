// Stand-in Renderer: clears every frame to one solid colour. The helper
// shows it when a scene cannot be read or has nothing the engine can draw;
// it also exercises the helper's whole path (surfaces, pacing, protocol,
// screenshots) in tests. GL calls go through libepoxy.
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
