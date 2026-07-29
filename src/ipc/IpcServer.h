// Unix socket IPC server; protocol documented in Protocol.h.
//
// Accepts multiple concurrent clients; each request line is parsed and
// dispatched to the handler callback, whose JSON string reply is written
// back followed by a newline. Client sockets are nonblocking and served on
// the daemon event loop.
#pragma once

#include <functional>
#include <map>
#include <string>

namespace canvas {

class EventLoop;

class IpcServer {
public:
    // handler receives the raw request line and returns the reply line
    // (without trailing newline).
    using Handler = std::function<std::string(const std::string& request)>;

    IpcServer(EventLoop& loop, Handler handler);
    ~IpcServer();

    bool listen(const std::string& path);

private:
    void acceptClient();
    void readClient(int fd);
    void closeClient(int fd);

    EventLoop& loop_;
    Handler handler_;
    int listen_fd_ = -1;
    std::string path_;
    std::map<int, std::string> client_buffers_;
};

}  // namespace canvas
