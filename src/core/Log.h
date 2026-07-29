// Minimal leveled logging to stderr.
//
// Usage: LOG_INFO("output {} ready", name); style formatting is not used to
// keep dependencies minimal; messages are built with stream syntax instead:
//   canvas::log::info() << "output " << name << " ready";
// Each statement produces one line, prefixed with a level tag and a
// monotonic timestamp. Level filtering is controlled by CANVAS_LOG
// (error, warn, info, debug; default info).
#pragma once

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace canvas::log {

enum class Level { Error = 0, Warn = 1, Info = 2, Debug = 3 };

inline Level threshold() {
    static Level lvl = [] {
        const char* env = std::getenv("CANVAS_LOG");
        std::string v = env ? env : "info";
        if (v == "error") return Level::Error;
        if (v == "warn") return Level::Warn;
        if (v == "debug") return Level::Debug;
        return Level::Info;
    }();
    return lvl;
}

// One log statement. Buffers the message and writes a single line on
// destruction so interleaving between threads stays per line.
class Line {
public:
    Line(Level lvl, const char* tag) : enabled_(lvl <= threshold()) {
        if (!enabled_) return;
        using namespace std::chrono;
        auto ms = duration_cast<milliseconds>(
                      steady_clock::now().time_since_epoch())
                      .count();
        buf_ << "[" << ms / 1000 << "." << ms % 1000 << "] [" << tag << "] ";
    }
    ~Line() {
        if (!enabled_) return;
        static std::mutex mu;
        std::lock_guard<std::mutex> lock(mu);
        std::cerr << buf_.str() << "\n";
    }
    template <typename T>
    Line& operator<<(const T& v) {
        if (enabled_) buf_ << v;
        return *this;
    }

private:
    bool enabled_;
    std::ostringstream buf_;
};

inline Line error() { return Line(Level::Error, "error"); }
inline Line warn() { return Line(Level::Warn, "warn"); }
inline Line info() { return Line(Level::Info, "info"); }
inline Line debug() { return Line(Level::Debug, "debug"); }

}  // namespace canvas::log
