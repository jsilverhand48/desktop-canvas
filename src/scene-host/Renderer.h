// What SceneHost drives once per tick: advance simulated time, then draw.
//
// The host owns surfaces, pacing and the daemon protocol; a Renderer only
// knows how to produce pixels. render() draws into whatever framebuffer is
// bound (a window surface's default framebuffer, or the screenshot FBO) at
// the given pixel size, with the GL context already current. Renderers must
// not depend on GL state surviving between calls.
//
// Today the only implementation is PlaceholderRenderer (a solid colour);
// the native scene engine (src/scene/, plan phases P1 onward) plugs in here.
#pragma once

#include <string>

namespace canvas::scenehost {

class Renderer {
public:
    virtual ~Renderer() = default;

    // Advances simulated time by dt seconds; called once per tick before
    // render() is called for each output.
    virtual void advance(double dt) = 0;
    virtual void render(int width, int height) = 0;

    // Live inputs from the daemon (process/HelperProtocol.h). Pointer
    // coordinates are output local logical pixels; property values use the
    // --set-property text form.
    virtual void setPointer(const std::string& /*output*/, double /*x*/,
                            double /*y*/) {}
    virtual void setProperty(const std::string& /*name*/,
                             const std::string& /*value*/) {}
    virtual void setMuted(bool /*muted*/) {}
    virtual void setVolume(int /*percent*/) {}
};

}  // namespace canvas::scenehost
