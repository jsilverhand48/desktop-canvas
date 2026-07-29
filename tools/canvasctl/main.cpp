// desktop-canvasctl: control CLI for the desktop-canvas daemon.
//
// Connects to the daemon's unix socket (ipc/Protocol.h), sends one JSON
// request line, prints the reply. Human friendly formatting for status and
// list; --json passes the raw reply through.
//
// Usage:
//   desktop-canvasctl status
//   desktop-canvasctl list [--json]
//   desktop-canvasctl set <output|*> <wallpaper id|none>
//   desktop-canvasctl playlist <file|none>
//   desktop-canvasctl rotate <seconds|off> [output ...]
//   desktop-canvasctl next
//   desktop-canvasctl pause | resume | mute | unmute | reload | quit
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ipc/Protocol.h"

using nlohmann::json;

namespace {

int usage() {
    std::cerr << "usage: desktop-canvasctl <status|list|set|playlist|rotate|"
                 "next|pause|resume|mute|unmute|reload|quit> [args] [--json]\n"
                 "       desktop-canvasctl set <output|*> <wallpaper id|none>\n"
                 "       desktop-canvasctl playlist <file|none>\n"
                 "       desktop-canvasctl rotate <seconds|off> [output ...]\n"
                 "       desktop-canvasctl next\n";
    return 2;
}

// Accepts a bare number of seconds or a suffixed duration (30s, 15m, 2h) so
// a rotation interval can be written the way a person says it.
bool parseDuration(const std::string& text, int& seconds) {
    if (text.empty()) return false;
    size_t end = 0;
    long value = 0;
    try {
        value = std::stol(text, &end);
    } catch (const std::exception&) {
        return false;
    }
    if (value < 0) return false;
    std::string suffix = text.substr(end);
    long multiplier = 1;
    if (suffix.empty() || suffix == "s") multiplier = 1;
    else if (suffix == "m") multiplier = 60;
    else if (suffix == "h") multiplier = 3600;
    else return false;
    seconds = static_cast<int>(value * multiplier);
    return true;
}

std::string sendRequest(const json& request) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return "";
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string path = canvas::ipc::socketPath();
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "cannot connect to " << path
                  << " (is desktop-canvas running?)\n";
        close(fd);
        return "";
    }
    std::string line = request.dump() + "\n";
    if (write(fd, line.data(), line.size()) < 0) {
        close(fd);
        return "";
    }
    std::string reply;
    char buf[65536];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        reply.append(buf, static_cast<size_t>(n));
        if (reply.find('\n') != std::string::npos) break;
    }
    close(fd);
    return reply;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    bool asJson = false;
    for (auto it = args.begin(); it != args.end();) {
        if (*it == "--json") {
            asJson = true;
            it = args.erase(it);
        } else {
            ++it;
        }
    }
    if (args.empty()) return usage();

    const std::string& cmd = args[0];
    json request = {{"cmd", cmd}};
    if (cmd == "set") {
        if (args.size() != 3) return usage();
        request["output"] = args[1];
        request["id"] = args[2] == "none" ? "" : args[2];
    } else if (cmd == "playlist") {
        if (args.size() != 2) return usage();
        request["file"] = args[1] == "none" ? "" : args[1];
    } else if (cmd == "rotate") {
        if (args.size() < 2) return usage();
        int seconds = 0;
        if (args[1] == "off") {
            seconds = 0;
        } else if (!parseDuration(args[1], seconds)) {
            std::cerr << "error: bad interval '" << args[1]
                      << "' (expected seconds, or 30s/15m/2h, or off)\n";
            return 2;
        }
        request["interval"] = seconds;
        // Any trailing words are output names; passing none rotates every
        // output, which is the documented default.
        request["outputs"] = std::vector<std::string>(args.begin() + 2,
                                                      args.end());
    } else if (cmd != "status" && cmd != "list" && cmd != "next" &&
               cmd != "pause" &&
               cmd != "resume" && cmd != "mute" && cmd != "unmute" &&
               cmd != "reload" && cmd != "quit") {
        return usage();
    }

    std::string reply = sendRequest(request);
    if (reply.empty()) return 1;
    json j = json::parse(reply, nullptr, false);
    if (j.is_discarded()) {
        std::cout << reply;
        return 1;
    }
    if (asJson) {
        std::cout << j.dump(2) << "\n";
        return j.value("ok", false) ? 0 : 1;
    }
    if (!j.value("ok", false)) {
        std::cerr << "error: " << j.value("error", "unknown") << "\n";
        return 1;
    }
    if (cmd == "status") {
        if (!j.contains("running") || j["running"].empty()) {
            std::cout << "no wallpapers running\n";
        } else {
            for (const auto& [output, id] : j["running"].items())
                std::cout << output << ": " << id.get<std::string>() << "\n";
        }
        std::string playlist = j.value("playlist", "");
        if (!playlist.empty()) {
            std::cout << "playlist: " << playlist << " ("
                      << j.value("playlist_size", 0) << " ids)\n";
            if (j.value("rotating", false)) {
                auto outputs = j.value("rotate_outputs",
                                       std::vector<std::string>{});
                std::cout << "rotating: every " << j.value("interval", 0)
                          << "s on ";
                if (outputs.empty()) {
                    std::cout << "all outputs";
                } else {
                    for (size_t i = 0; i < outputs.size(); i++)
                        std::cout << (i ? ", " : "") << outputs[i];
                }
                std::cout << "; next in " << j.value("next_in", 0) << "s\n";
            } else {
                std::cout << "rotating: off\n";
            }
        }
    } else if (cmd == "next") {
        std::cout << "rotated to " << j.value("id", "") << "\n";
    } else if (cmd == "list") {
        for (const auto& item : j.value("items", json::array())) {
            std::cout << std::left << std::setw(12)
                      << item.value("id", "") << std::setw(12)
                      << item.value("type", "") << item.value("title", "");
            std::string reason = item.value("reason", "");
            if (!reason.empty()) std::cout << "  [" << reason << "]";
            std::cout << "\n";
        }
    } else {
        std::cout << "ok\n";
    }
    return 0;
}
