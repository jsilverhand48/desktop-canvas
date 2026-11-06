// Implementation of MprisWatcher declared in MprisWatcher.h.
//
// sd-bus runs on the EventLoop like KdeBridge: its fd is watched with the
// events sd_bus_get_events() asks for, its timeout becomes an addTimerAt
// deadline, and process() drains sd_bus_process() and re-arms both.
// Asynchronous calls keep their slot in pending_ (slot to player name);
// slots are released after sd_bus_process() returns, never inside their
// own callback.
#include "desktop/MprisWatcher.h"

#include <poll.h>
#include <sys/epoll.h>
#include <systemd/sd-bus.h>

#include <cctype>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include "core/EventLoop.h"
#include "core/Log.h"

namespace canvas {

namespace {

constexpr const char* kPrefix = "org.mpris.MediaPlayer2.";
constexpr const char* kPath = "/org/mpris/MediaPlayer2";
constexpr const char* kPlayer = "org.mpris.MediaPlayer2.Player";

std::vector<sd_bus_slot*> g_finished;  // slots whose call completed, released after processing

bool isPlayer(const char* name) { return name && std::strncmp(name, kPrefix, std::strlen(kPrefix)) == 0; }

std::string join(char** list) {
    std::string out;
    for (char** p = list; p && *p; p++) out += (out.empty() ? "" : ", ") + std::string(*p);
    return out;
}

// Reads the variant at m into text or number, skipping other types.
void readVariant(sd_bus_message* m, std::string* text, double* number) {
    char type = 0;
    const char* contents = nullptr;
    if (sd_bus_message_peek_type(m, &type, &contents) <= 0 || type != 'v' || !contents) {
        sd_bus_message_skip(m, nullptr);
        return;
    }
    const std::string sig = contents;
    if (sd_bus_message_enter_container(m, 'v', contents) <= 0) return;
    if ((sig == "s" || sig == "o") && text) {
        const char* s = nullptr;
        if (sd_bus_message_read(m, sig.c_str(), &s) > 0 && s) *text = s;
    } else if (sig == "as" && text) {
        char** list = nullptr;
        if (sd_bus_message_read_strv(m, &list) >= 0) {
            *text = join(list);
            for (char** p = list; p && *p; p++) free(*p);
            free(list);
        }
    } else if (number && (sig == "x" || sig == "t" || sig == "i" || sig == "u" || sig == "d")) {
        if (sig == "x") {
            int64_t v = 0;
            sd_bus_message_read(m, "x", &v);
            *number = double(v);
        } else if (sig == "t") {
            uint64_t v = 0;
            sd_bus_message_read(m, "t", &v);
            *number = double(v);
        } else if (sig == "i") {
            int32_t v = 0;
            sd_bus_message_read(m, "i", &v);
            *number = double(v);
        } else if (sig == "u") {
            uint32_t v = 0;
            sd_bus_message_read(m, "u", &v);
            *number = double(v);
        } else {
            sd_bus_message_read(m, "d", number);
        }
    } else {
        sd_bus_message_skip(m, sig.c_str());
    }
    sd_bus_message_exit_container(m);
}

}  // namespace

MprisWatcher::MprisWatcher(EventLoop& loop, Callback onChange) : loop_(loop), onChange_(std::move(onChange)) {}

MprisWatcher::~MprisWatcher() { stop(); }

bool MprisWatcher::start() {
    if (bus_) return true;
    if (sd_bus_open_user(&bus_) < 0) {
        bus_ = nullptr;
        log::warn() << "media players: no session bus";
        return false;
    }
    sd_bus_match_signal(bus_, nullptr, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                        "NameOwnerChanged", &MprisWatcher::onNameOwner, this);
    sd_bus_match_signal(bus_, nullptr, nullptr, kPath, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                        &MprisWatcher::onPropertiesChanged, this);
    fd_ = sd_bus_get_fd(bus_);
    loop_.addFd(fd_, EPOLLIN, [this](uint32_t) { process(); });
    listNames();
    publish();
    process();
    return true;
}

void MprisWatcher::stop() {
    if (!bus_) return;
    if (busTimer_) loop_.cancelTimer(busTimer_);
    if (positionTimer_) loop_.cancelTimer(positionTimer_);
    busTimer_ = positionTimer_ = 0;
    if (fd_ >= 0) loop_.removeFd(fd_);
    fd_ = -1;
    for (auto& [slot, name] : pending_) sd_bus_slot_unref(slot);
    pending_.clear();
    for (sd_bus_slot* s : g_finished) sd_bus_slot_unref(s);
    g_finished.clear();
    sd_bus_flush_close_unref(bus_);
    bus_ = nullptr;
    players_.clear();
    last_ = nullptr;
}

void MprisWatcher::process() {
    if (!bus_) return;
    for (;;) {
        const int r = sd_bus_process(bus_, nullptr);
        if (r < 0) {
            log::warn() << "media players: bus connection lost";
            stop();
            return;
        }
        if (r == 0) break;
    }
    for (sd_bus_slot* s : g_finished) sd_bus_slot_unref(s);
    g_finished.clear();
    rearm();
}

void MprisWatcher::rearm() {
    if (!bus_ || fd_ < 0) return;
    const int events = sd_bus_get_events(bus_);
    uint32_t mask = EPOLLIN;
    if (events > 0 && (events & POLLOUT)) mask |= EPOLLOUT;
    loop_.modifyFd(fd_, mask);
    if (busTimer_) loop_.cancelTimer(busTimer_);
    busTimer_ = 0;
    uint64_t usec = 0;
    if (sd_bus_get_timeout(bus_, &usec) >= 0 && usec != UINT64_MAX) {
        const auto deadline = std::chrono::steady_clock::time_point(std::chrono::microseconds(usec));
        busTimer_ = loop_.addTimerAt(deadline, [this] {
            busTimer_ = 0;
            process();
        });
    }
}

void MprisWatcher::listNames() {
    sd_bus_slot* slot = nullptr;
    if (sd_bus_call_method_async(bus_, &slot, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                 "ListNames", &MprisWatcher::onListNames, this, "") >= 0)
        pending_[slot] = "";
}

void MprisWatcher::fetch(const std::string& name) {
    sd_bus_slot* slot = nullptr;
    if (sd_bus_call_method_async(bus_, &slot, name.c_str(), kPath, "org.freedesktop.DBus.Properties", "GetAll",
                                 &MprisWatcher::onGetAll, this, "s", kPlayer) >= 0)
        pending_[slot] = name;
}

// The slot of the call being answered: its player name, released later.
static std::string takePending(sd_bus_message* m, std::map<sd_bus_slot*, std::string>& pending) {
    sd_bus_slot* slot = sd_bus_get_current_slot(sd_bus_message_get_bus(m));
    auto it = pending.find(slot);
    if (it == pending.end()) return {};
    std::string name = it->second;
    pending.erase(it);
    g_finished.push_back(slot);
    return name;
}

int MprisWatcher::onListNames(sd_bus_message* m, void* self, sd_bus_error*) {
    auto* me = static_cast<MprisWatcher*>(self);
    takePending(m, me->pending_);
    if (sd_bus_message_is_method_error(m, nullptr)) return 0;
    char** names = nullptr;
    if (sd_bus_message_read_strv(m, &names) < 0) return 0;
    for (char** p = names; p && *p; p++) {
        if (isPlayer(*p)) {
            sd_bus_slot* slot = nullptr;
            if (sd_bus_call_method_async(me->bus_, &slot, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                         "org.freedesktop.DBus", "GetNameOwner", &MprisWatcher::onOwner, me, "s",
                                         *p) >= 0)
                me->pending_[slot] = *p;
            me->players_[*p];
            me->fetch(*p);
        }
        free(*p);
    }
    free(names);
    return 0;
}

int MprisWatcher::onOwner(sd_bus_message* m, void* self, sd_bus_error*) {
    auto* me = static_cast<MprisWatcher*>(self);
    const std::string name = takePending(m, me->pending_);
    const char* owner = nullptr;
    if (!sd_bus_message_is_method_error(m, nullptr) && sd_bus_message_read(m, "s", &owner) > 0 && owner) {
        auto it = me->players_.find(name);
        if (it != me->players_.end()) it->second.owner = owner;
    }
    return 0;
}

int MprisWatcher::onNameOwner(sd_bus_message* m, void* self, sd_bus_error*) {
    auto* me = static_cast<MprisWatcher*>(self);
    const char *name = nullptr, *before = nullptr, *after = nullptr;
    if (sd_bus_message_read(m, "sss", &name, &before, &after) < 0 || !isPlayer(name)) return 0;
    if (!after || !*after) {
        me->players_.erase(name);
        me->publish();
    } else {
        me->players_[name].owner = after;
        me->fetch(name);
    }
    return 0;
}

int MprisWatcher::onPropertiesChanged(sd_bus_message* m, void* self, sd_bus_error*) {
    auto* me = static_cast<MprisWatcher*>(self);
    const char* interface = nullptr;
    if (sd_bus_message_read(m, "s", &interface) < 0 || !interface || std::strcmp(interface, kPlayer) != 0) return 0;
    const char* sender = sd_bus_message_get_sender(m);
    for (auto& [name, player] : me->players_)
        if (sender && player.owner == sender) me->fetch(name);
    return 0;
}

int MprisWatcher::onGetAll(sd_bus_message* m, void* self, sd_bus_error*) {
    auto* me = static_cast<MprisWatcher*>(self);
    const std::string name = takePending(m, me->pending_);
    auto it = me->players_.find(name);
    if (it == me->players_.end() || sd_bus_message_is_method_error(m, nullptr)) return 0;
    me->parseProperties(it->second, m);
    me->publish();
    return 0;
}

void MprisWatcher::parseProperties(Player& p, sd_bus_message* m) {
    if (sd_bus_message_enter_container(m, 'a', "{sv}") <= 0) return;
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char* key = nullptr;
        sd_bus_message_read(m, "s", &key);
        const std::string k = key ? key : "";
        if (k == "PlaybackStatus") {
            std::string status;
            readVariant(m, &status, nullptr);
            if (status != p.status) {
                if (status == "Playing") p.playOrder = ++order_;
                if (status == "Paused") p.pauseOrder = ++order_;
                p.status = status;
            }
        } else if (k == "Position") {
            double us = 0;
            readVariant(m, nullptr, &us);
            p.position = us / 1e6;
        } else if (k == "Metadata" && sd_bus_message_enter_container(m, 'v', "a{sv}") > 0) {
            p.title = p.artist = p.album = p.albumArtist = p.genres = p.artUrl = "";
            p.length = 0;
            if (sd_bus_message_enter_container(m, 'a', "{sv}") > 0) {
                while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
                    const char* mk = nullptr;
                    sd_bus_message_read(m, "s", &mk);
                    const std::string key2 = mk ? mk : "";
                    double us = 0;
                    if (key2 == "xesam:title") readVariant(m, &p.title, nullptr);
                    else if (key2 == "xesam:artist") readVariant(m, &p.artist, nullptr);
                    else if (key2 == "xesam:album") readVariant(m, &p.album, nullptr);
                    else if (key2 == "xesam:albumArtist") readVariant(m, &p.albumArtist, nullptr);
                    else if (key2 == "xesam:genre") readVariant(m, &p.genres, nullptr);
                    else if (key2 == "mpris:artUrl") readVariant(m, &p.artUrl, nullptr);
                    else if (key2 == "mpris:length") {
                        readVariant(m, nullptr, &us);
                        p.length = us / 1e6;
                    } else {
                        sd_bus_message_skip(m, "v");
                    }
                    sd_bus_message_exit_container(m);
                }
                sd_bus_message_exit_container(m);
            }
            sd_bus_message_exit_container(m);
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
}

void MprisWatcher::askPosition() {
    positionTimer_ = 0;
    const nlohmann::json now = describe(players_, timeline_);
    if (now.value("state", 0) != 1) return;
    const std::string name = now.value("player", "");
    sd_bus_slot* slot = nullptr;
    if (sd_bus_call_method_async(bus_, &slot, name.c_str(), kPath, "org.freedesktop.DBus.Properties", "Get",
                                 &MprisWatcher::onPosition, this, "ss", kPlayer, "Position") >= 0)
        pending_[slot] = name;
    rearm();
}

int MprisWatcher::onPosition(sd_bus_message* m, void* self, sd_bus_error*) {
    auto* me = static_cast<MprisWatcher*>(self);
    const std::string name = takePending(m, me->pending_);
    auto it = me->players_.find(name);
    if (it == me->players_.end() || sd_bus_message_is_method_error(m, nullptr)) return 0;
    double us = 0;
    readVariant(m, nullptr, &us);
    it->second.position = us / 1e6;
    me->timeline_++;
    me->publish();
    return 0;
}

void MprisWatcher::publish() {
    nlohmann::json now = describe(players_, timeline_);
    // While playing, the position is asked again in a second.
    if (now.value("state", 0) == 1 && !positionTimer_ && bus_)
        positionTimer_ = loop_.addTimer(1000, [this] { askPosition(); });
    now.erase("player");
    if (now == last_) return;
    last_ = now;
    if (onChange_) onChange_(now);
}

std::string MprisWatcher::localPath(const std::string& url) {
    const std::string scheme = "file://";
    if (url.compare(0, scheme.size(), scheme) != 0) return {};
    std::string path;
    for (size_t i = scheme.size(); i < url.size(); i++) {
        if (url[i] == '%' && i + 2 < url.size() && std::isxdigit(static_cast<unsigned char>(url[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(url[i + 2]))) {
            path += char(std::stoi(url.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            path += url[i];
        }
    }
    return !path.empty() && path[0] == '/' ? path : std::string();
}

nlohmann::json MprisWatcher::describe(const std::map<std::string, Player>& players, uint64_t timeline) {
    const std::pair<const std::string, Player>* followed = nullptr;
    for (const auto& entry : players)
        if (entry.second.status == "Playing" && (!followed || entry.second.playOrder > followed->second.playOrder))
            followed = &entry;
    if (!followed)
        for (const auto& entry : players)
            if (entry.second.status == "Paused" &&
                (!followed || entry.second.pauseOrder > followed->second.pauseOrder))
                followed = &entry;
    nlohmann::json j = {{"enabled", true}, {"state", 0}, {"title", ""}, {"artist", ""}, {"subTitle", ""},
                        {"albumTitle", ""}, {"albumArtist", ""}, {"genres", ""}, {"contentType", ""},
                        {"thumbnail", ""}, {"position", 0.0}, {"duration", 0.0}, {"timeline", timeline}};
    if (!followed) return j;
    const Player& p = followed->second;
    j["player"] = followed->first;
    j["state"] = p.status == "Playing" ? 1 : 2;
    j["title"] = p.title;
    j["artist"] = p.artist;
    j["albumTitle"] = p.album;
    j["albumArtist"] = p.albumArtist;
    j["genres"] = p.genres;
    j["contentType"] = "audio";
    j["thumbnail"] = localPath(p.artUrl);
    j["position"] = p.position;
    j["duration"] = p.length;
    return j;
}

}  // namespace canvas
