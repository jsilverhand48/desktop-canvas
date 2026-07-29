// Implementation of IpcServer declared in IpcServer.h.
#include "ipc/IpcServer.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>

#include "core/EventLoop.h"
#include "core/Log.h"

namespace canvas {

IpcServer::IpcServer(EventLoop& loop, Handler handler)
    : loop_(loop), handler_(std::move(handler)) {}

IpcServer::~IpcServer() {
    for (auto& [fd, buf] : client_buffers_) {
        loop_.removeFd(fd);
        close(fd);
    }
    if (listen_fd_ >= 0) {
        loop_.removeFd(listen_fd_);
        close(listen_fd_);
        unlink(path_.c_str());
    }
}

bool IpcServer::listen(const std::string& path) {
    path_ = path;
    listen_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) return false;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    unlink(path.c_str());  // stale socket from a previous run
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) <
        0) {
        log::error() << "ipc bind failed on " << path << ": "
                     << strerror(errno);
        return false;
    }
    if (::listen(listen_fd_, 8) < 0) return false;
    loop_.addFd(listen_fd_, EPOLLIN, [this](uint32_t) { acceptClient(); });
    log::info() << "ipc listening on " << path;
    return true;
}

void IpcServer::acceptClient() {
    int fd = accept4(listen_fd_, nullptr, nullptr,
                     SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) return;
    client_buffers_[fd] = "";
    loop_.addFd(fd, EPOLLIN | EPOLLHUP, [this, fd](uint32_t events) {
        if (events & (EPOLLHUP | EPOLLERR)) {
            closeClient(fd);
            return;
        }
        readClient(fd);
    });
}

void IpcServer::readClient(int fd) {
    char buf[4096];
    while (true) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            client_buffers_[fd].append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            closeClient(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            closeClient(fd);
            return;
        }
    }
    auto& pending = client_buffers_[fd];
    size_t pos;
    while ((pos = pending.find('\n')) != std::string::npos) {
        std::string line = pending.substr(0, pos);
        pending.erase(0, pos + 1);
        if (line.empty()) continue;
        std::string reply = handler_(line) + "\n";
        // Replies are small; a short write to a dead client just drops it.
        if (write(fd, reply.data(), reply.size()) < 0) {
            closeClient(fd);
            return;
        }
    }
}

void IpcServer::closeClient(int fd) {
    loop_.removeFd(fd);
    close(fd);
    client_buffers_.erase(fd);
}

}  // namespace canvas
