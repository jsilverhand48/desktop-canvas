// Message channel between two processes over a SOCK_SEQPACKET unix socket.
//
// One datagram is one message, so framing comes from the kernel: order is
// preserved, boundaries are kept, and a peer that closes or exits reads as
// end of file. Messages are opaque byte strings here; the daemon and helper
// layer JSON on top (process/HelperProtocol.h).
//
// Contract:
// - The channel owns its fd and closes it in close() or the destructor.
// - send() never blocks. When the socket buffer is full, messages wait in a
//   queue drained on EPOLLOUT; past kMaxQueued queued messages new ones are
//   dropped (a peer that stops reading is handled by its watchdog, not by
//   unbounded memory here).
// - Messages larger than kMaxMessage are refused by send() and dropped on
//   receive (the kernel truncates them; the rest of the datagram is lost).
// - Callbacks run on the EventLoop thread. A callback may destroy the
//   channel; delivery stops immediately in that case.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace canvas {

class EventLoop;

class ControlChannel {
public:
    static constexpr size_t kMaxMessage = 64 * 1024;
    static constexpr size_t kMaxQueued = 256;

    // Takes ownership of fd, a connected SOCK_SEQPACKET socket, and makes it
    // non blocking.
    ControlChannel(EventLoop& loop, int fd);
    ~ControlChannel();
    ControlChannel(const ControlChannel&) = delete;
    ControlChannel& operator=(const ControlChannel&) = delete;

    // Creates a connected pair, both ends SOCK_CLOEXEC. Returns false (and
    // leaves fds untouched) on failure.
    static bool makePair(int fds[2]);

    void setOnMessage(std::function<void(std::string)> cb) {
        on_message_ = std::move(cb);
    }
    // Fires once, when the peer closes, errors, or close() is called.
    void setOnClosed(std::function<void()> cb) { on_closed_ = std::move(cb); }

    // Queues message for the peer. False when the channel is closed, the
    // message is too large, or the queue is full (message dropped).
    bool send(std::string message);

    bool isOpen() const { return fd_ >= 0; }
    void close();

private:
    void handleEvents(uint32_t events);
    void flush();
    void updateInterest();

    EventLoop& loop_;
    int fd_ = -1;
    bool want_write_ = false;
    std::deque<std::string> queue_;
    std::function<void(std::string)> on_message_;
    std::function<void()> on_closed_;
    // Cleared by the destructor so a callback that destroys the channel can
    // be detected by the code that invoked it.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace canvas
