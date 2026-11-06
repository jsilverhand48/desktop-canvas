// What SceneHost drives once per tick: advance simulated time, then draw.
//
// The host owns surfaces, pacing and the daemon protocol; a Renderer only
// knows how to produce pixels. render() draws into whatever framebuffer is
// bound (a window surface's default framebuffer, or the screenshot FBO) at
// the given pixel size, with the GL context already current. Renderers must
// not depend on GL state surviving between calls.
//
// Implementations: NativeRenderer (the scene engine, src/scene/) and
// PlaceholderRenderer (a solid colour when the scene cannot be drawn).
#pragma once

#include <cstdint>
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
    // Linux input button codes (BTN_LEFT = 272) at the last pointer position.
    virtual void setPointerButton(const std::string& /*output*/,
                                  uint32_t /*button*/, bool /*pressed*/) {}
    virtual void setProperty(const std::string& /*name*/,
                             const std::string& /*value*/) {}
    virtual void setMuted(bool /*muted*/) {}
    virtual void setVolume(int /*percent*/) {}
    // The host stopped ticking (paused, or every output covered): sounds
    // pause with it.
    virtual void setPaused(bool /*paused*/) {}
    // True while the scene reads the audio spectrum (the host asks the
    // daemon for capture, src/scene/audio/README.md section 3).
    virtual bool wantsAudio() const { return false; }
};

}  // namespace canvas::scenehost
