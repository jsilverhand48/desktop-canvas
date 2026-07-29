// WallpaperInstance backed by a supervised child process.
//
// Used for scene wallpapers (linux-wallpaperengine) and web wallpapers
// (desktop-canvas-web helper). The child creates its own surface: a layer
// shell surface on Wayland, or a window the daemon demotes to the desktop
// layer on X11 (X11Connection::demoteWindowsOf). The argv is produced by a
// builder function of the current mute state, because mute is a command
// line flag for these children; mute changes therefore restart the child.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "render/WallpaperInstance.h"

namespace canvas {

class ChildSupervisor;

class ExternalProcessInstance : public WallpaperInstance {
public:
    using ArgvBuilder = std::function<std::vector<std::string>(bool muted)>;

    // tag = "<kind>:<output>", e.g. "scene:DP-1", "web:HDMI-A-1"; the part
    // after ':' is used by InstanceManager to locate the slot on failure.
    ExternalProcessInstance(ChildSupervisor& supervisor, std::string tag,
                            std::string outputName, std::string wallpaperId,
                            bool muted, ArgvBuilder builder);
    ~ExternalProcessInstance() override;

    void start() override;
    void stop() override;
    void pause() override;   // SIGSTOP the child
    void resume() override;  // SIGCONT the child
    void setMuted(bool muted) override;  // restarts the child

    const std::string& wallpaperId() const override { return wallpaper_id_; }
    const std::string& outputName() const override { return output_name_; }
    const std::string& tag() const { return tag_; }

private:
    ChildSupervisor& supervisor_;
    std::string tag_;
    std::string output_name_;
    std::string wallpaper_id_;
    bool muted_;
    ArgvBuilder builder_;
    bool started_ = false;
};

}  // namespace canvas
