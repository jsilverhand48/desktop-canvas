// IPC protocol shared by the daemon (IpcServer) and desktop-canvasctl.
//
// Transport: unix stream socket at $XDG_RUNTIME_DIR/desktop-canvas.sock
// (fallback /tmp/desktop-canvas-<uid>.sock). Framing: one JSON object per
// line in each direction.
//
// Requests:  {"cmd": "status"}
//            {"cmd": "list"}
//            {"cmd": "set", "output": "DP-1"|"*", "id": "<workshop id>"|""}
//            {"cmd": "pause"} {"cmd": "resume"}
//            {"cmd": "mute"} {"cmd": "unmute"}
//            {"cmd": "reload"}   rescan the workshop directories
//            {"cmd": "playlist", "file": "<path>"|""}  ids file for rotation
//            {"cmd": "rotate", "interval": <seconds>,  0 stops rotation
//                              "outputs": ["DP-1", ...]}   optional, [] = all
//            {"cmd": "next"}     apply the next random pick immediately
//            {"cmd": "quit"}
// Responses: {"ok": true, ...payload} or {"ok": false, "error": "..."}
#pragma once

#include <cstdlib>
#include <string>
#include <unistd.h>

namespace canvas::ipc {

inline std::string socketPath() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime && *runtime)
        return std::string(runtime) + "/desktop-canvas.sock";
    return "/tmp/desktop-canvas-" + std::to_string(getuid()) + ".sock";
}

}  // namespace canvas::ipc
