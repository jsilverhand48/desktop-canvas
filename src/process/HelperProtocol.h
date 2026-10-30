// Protocol between the daemon and its own helper processes
// (desktop-canvas-scene; see src/scene-host/README.md).
//
// Transport: core/ControlChannel over a SOCK_SEQPACKET socketpair. The
// daemon keeps one end; the helper gets the other as fd kControlFd and is
// told so with --control-fd. Each message is one JSON object carrying a
// "type" string, at most ControlChannel::kMaxMessage bytes. Both sides
// ignore unknown types and unknown fields (forward compatibility); anything
// that is not such an object, or nests deeper than kMaxDepth, is dropped.
// The daemon treats every helper message as untrusted input.
//
// Daemon to helper:
//   {"type":"pause"}   {"type":"resume"}
//   {"type":"mute","muted":true}
//   {"type":"volume","volume":0..100}
//   {"type":"pointer","output":"DP-1","x":12.5,"y":40}
//       output local logical coordinates; only the latest position matters
//   {"type":"props","values":{"name":"text value",...}}
//       user property overrides, values in --set-property text form
//   {"type":"covered","output":"DP-1","covered":true}
//       a fullscreen window hides that output (nothing to present there)
//   {"type":"quit"}    exit with kExitClean promptly
// Helper to daemon:
//   {"type":"hello","proto":1,"pid":123}     first message after start
//   {"type":"ready"}                         first frame on every output
//   {"type":"unsupported","features":["..."]}
//       sent before exiting with kExitUnsupported (strict mode)
//   {"type":"error","message":"..."}
//   {"type":"stats","fps":30.0,"cpu_ms":1.5,"gpu_ms":0.8,"rss":123456}
//       every kStatsIntervalMs while running; doubles as the heartbeat
//
// Exit codes, honoured by ChildSupervisor for children started with
// ExitPolicy::HelperProtocol:
//   kExitClean        quit, SIGTERM or end of work: not respawned
//   kExitUsage        bad command line: not respawned, logged
//   kExitUnsupported  content needs unsupported features: not respawned and
//                     not counted as a failure (the daemon falls back)
//   anything else, or death by signal: a crash, respawned with backoff
#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace canvas::helper {

inline constexpr int kProtocolVersion = 1;
inline constexpr int kControlFd = 3;

inline constexpr int kExitClean = 0;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitUnsupported = 3;

inline constexpr int kStatsIntervalMs = 2000;
// A helper silent for this long while not paused is considered hung.
inline constexpr int kHeartbeatTimeoutMs = 10000;

inline constexpr int kMaxDepth = 16;

// Nesting depth of a JSON text (arrays and objects), ignoring brackets
// inside strings. Lets decode() refuse hostile nesting before the parser
// (and the destructor of the parsed value) recurse on it.
inline int jsonDepth(const std::string& text) {
    int depth = 0;
    int deepest = 0;
    bool inString = false;
    bool escaped = false;
    for (char c : text) {
        if (inString) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > deepest) deepest = depth;
        } else if (c == ']' || c == '}') {
            --depth;
        }
    }
    return deepest;
}

inline std::string encode(const nlohmann::json& message) {
    return message.dump();
}

// The message, or nullopt unless data is a JSON object with a string
// "type" nested at most kMaxDepth deep.
inline std::optional<nlohmann::json> decode(const std::string& data) {
    if (jsonDepth(data) > kMaxDepth) return std::nullopt;
    nlohmann::json j = nlohmann::json::parse(data, nullptr, false);
    if (!j.is_object()) return std::nullopt;
    auto it = j.find("type");
    if (it == j.end() || !it->is_string()) return std::nullopt;
    return j;
}

// "type" of a decoded message.
inline std::string typeOf(const nlohmann::json& message) {
    return message.at("type").get<std::string>();
}

}  // namespace canvas::helper
