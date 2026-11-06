// Media players over MPRIS on the session bus, for scene script media
// events (src/scene/audio/README.md section 7).
//
// Players are the bus names org.mpris.MediaPlayer2.*; each one's
// org.mpris.MediaPlayer2.Player properties (PlaybackStatus, Metadata) are
// read with GetAll when it appears and again whenever it signals
// PropertiesChanged; while the followed player plays, its Position is
// asked once a second (MPRIS does not signal it). The followed player is
// the one that started playing last, else the one paused last. Every
// change becomes one "media" message body (process/HelperProtocol.h):
// enabled, state, title, artist, subTitle, albumTitle, albumArtist,
// genres, contentType, thumbnail (a local file path from mpris:artUrl,
// empty for other schemes: nothing is fetched), position, duration and a
// timeline serial. Every bus call is asynchronous, so a stuck player
// never blocks the daemon. Built only with sd-bus (CANVAS_HAVE_SDBUS).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

struct sd_bus;
struct sd_bus_message;
struct sd_bus_slot;
struct sd_bus_error;

namespace canvas {

class EventLoop;

class MprisWatcher {
public:
    using Callback = std::function<void(const nlohmann::json& media)>;
    MprisWatcher(EventLoop& loop, Callback onChange);
    ~MprisWatcher();
    MprisWatcher(const MprisWatcher&) = delete;
    MprisWatcher& operator=(const MprisWatcher&) = delete;

    bool start();
    void stop();

    // Parsed player state, public for tests.
    struct Player {
        std::string owner;  // unique bus name
        std::string status = "Stopped";
        std::string title, artist, album, albumArtist, genres, artUrl;
        double length = 0, position = 0;
        uint64_t playOrder = 0, pauseOrder = 0;  // when it last started or paused
    };
    // The message for a set of players (the followed one chosen as above).
    static nlohmann::json describe(const std::map<std::string, Player>& players, uint64_t timeline);
    // file:// URLs to local paths (percent decoded); anything else empty.
    static std::string localPath(const std::string& url);

private:
    void process();
    void rearm();
    void listNames();
    void fetch(const std::string& name);
    void askPosition();
    void publish();
    void parseProperties(Player& p, sd_bus_message* m);

    static int onNameOwner(sd_bus_message* m, void* self, sd_bus_error* error);
    static int onPropertiesChanged(sd_bus_message* m, void* self, sd_bus_error* error);
    static int onListNames(sd_bus_message* m, void* self, sd_bus_error* error);
    static int onGetAll(sd_bus_message* m, void* self, sd_bus_error* error);
    static int onOwner(sd_bus_message* m, void* self, sd_bus_error* error);
    static int onPosition(sd_bus_message* m, void* self, sd_bus_error* error);

    EventLoop& loop_;
    Callback onChange_;
    sd_bus* bus_ = nullptr;
    int fd_ = -1;
    uint64_t busTimer_ = 0, positionTimer_ = 0;
    std::map<std::string, Player> players_;  // by well known name
    std::map<sd_bus_slot*, std::string> pending_;  // asynchronous calls: slot to player name
    uint64_t order_ = 0;
    uint64_t timeline_ = 0;
    nlohmann::json last_;
};

}  // namespace canvas
