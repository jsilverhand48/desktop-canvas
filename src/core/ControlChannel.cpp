// Implementation of ControlChannel declared in ControlChannel.h.
//
// Reads drain every pending datagram per wakeup with a kMaxMessage + 1 byte
// buffer: a datagram that fills it was truncated by the kernel and is
// dropped. Writes go straight to the socket while it accepts them and fall
// back to the queue plus EPOLLOUT interest on EAGAIN.
#include "core/ControlChannel.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

#include "core/EventLoop.h"
#include "core/Log.h"

namespace canvas {

ControlChannel::ControlChannel(EventLoop& loop, int fd) : loop_(loop), fd_(fd) {
    if (fd_ < 0) return;
    int flags = fcntl(fd_, F_GETFL);
    if (flags >= 0) fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    loop_.addFd(fd_, EPOLLIN | EPOLLRDHUP,
                [this](uint32_t events) { handleEvents(events); });
}

ControlChannel::~ControlChannel() {
    *alive_ = false;
    on_closed_ = nullptr;  // destruction is not a peer close
    close();
}

bool ControlChannel::makePair(int fds[2]) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) < 0) {
        log::error() << "socketpair failed: " << strerror(errno);
        return false;
    }
    fds[0] = pair[0];
    fds[1] = pair[1];
    return true;
}

bool ControlChannel::send(std::string message) {
    if (fd_ < 0) return false;
    if (message.size() > kMaxMessage) {
        log::warn() << "control message of " << message.size()
                    << " bytes exceeds the " << kMaxMessage << " byte limit";
        return false;
    }
    if (queue_.size() >= kMaxQueued) {
        log::warn() << "control channel queue full, dropping a message";
        return false;
    }
    queue_.push_back(std::move(message));
    flush();
    return true;
}

void ControlChannel::flush() {
    while (fd_ >= 0 && !queue_.empty()) {
        const std::string& front = queue_.front();
        ssize_t n = ::send(fd_, front.data(), front.size(),
                           MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n >= 0) {
            queue_.pop_front();
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        // EPIPE, ECONNRESET: the peer is gone; the read side reports it.
        queue_.clear();
        break;
    }
    updateInterest();
}

void ControlChannel::updateInterest() {
    if (fd_ < 0) return;
    bool want = !queue_.empty();
    if (want == want_write_) return;
    want_write_ = want;
    loop_.modifyFd(fd_, EPOLLIN | EPOLLRDHUP | (want ? EPOLLOUT : 0u));
}

void ControlChannel::handleEvents(uint32_t events) {
    auto alive = alive_;
    if (events & EPOLLOUT) flush();

    std::vector<char> buffer(kMaxMessage + 1);
    bool peerClosed = false;
    while (fd_ >= 0) {
        ssize_t n = recv(fd_, buffer.data(), buffer.size(), MSG_DONTWAIT);
        if (n > 0) {
            if (static_cast<size_t>(n) > kMaxMessage) {
                log::warn() << "dropping oversized control message";
                continue;
            }
            if (on_message_) {
                auto cb = on_message_;  // the callback may replace itself
                cb(std::string(buffer.data(), static_cast<size_t>(n)));
                if (!*alive) return;
            }
            continue;
        }
        if (n == 0) {  // orderly shutdown by the peer
            peerClosed = true;
            break;
        }
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) peerClosed = true;
        break;
    }
    if (events & (EPOLLHUP | EPOLLERR)) peerClosed = true;
    if (peerClosed) close();
}

void ControlChannel::close() {
    if (fd_ < 0) return;
    loop_.removeFd(fd_);
    ::close(fd_);
    fd_ = -1;
    queue_.clear();
    want_write_ = false;
    if (on_closed_) {
        auto cb = std::move(on_closed_);
        on_closed_ = nullptr;
        cb();
    }
}

}  // namespace canvas
