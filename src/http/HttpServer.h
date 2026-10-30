// Minimal HTTP/1.1 server on the daemon event loop, for the settings UI.
//
// Scope is deliberately small: it serves one local browser.
// - Listens on 127.0.0.1 only. Nothing else on the network can connect.
// - One request per connection (responses carry "Connection: close").
// - Request bodies need Content-Length (no chunked uploads), capped at
//   kMaxBody; headers are capped at kMaxHeader.
// - Responses are either an in-memory body or a file streamed in chunks
//   with nonblocking writes, so a multi-MB preview never stalls the loop.
// - Idle connections are closed after kIdleMs.
//
// Localhost is not a security boundary for browsers, so every request must
// carry Host 127.0.0.1:<port> or localhost:<port> (blocks DNS rebinding:
// another site's hostname resolved to 127.0.0.1), and state changing
// methods (anything but GET/HEAD) with an Origin header must come from that
// same origin (blocks cross-site requests from other open tabs). Requests
// without Origin (curl, scripts) are allowed. Rejections answer 403.
//
// parseRequest(), hostAllowed() and originAllowed() are pure functions so
// they can be unit tested without sockets.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace canvas {

class EventLoop;

struct HttpRequest {
    std::string method;                         // "GET", "PUT", ...
    std::string path;                           // decoded, no query
    std::map<std::string, std::string> query;   // decoded
    std::map<std::string, std::string> headers; // lower case names
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
    // When set, the file's contents are streamed instead of `body`.
    std::filesystem::path file;
    std::vector<std::pair<std::string, std::string>> headers;

    static HttpResponse json(int status, std::string body);
    static HttpResponse text(int status, std::string body);
};

enum class ParseStatus { Incomplete, Complete, Bad, TooLarge };

constexpr size_t kMaxHeader = 16 * 1024;
constexpr size_t kMaxBody = 1024 * 1024;
constexpr uint64_t kIdleMs = 30000;

// Parses one request from the start of buffer.
ParseStatus parseRequest(const std::string& buffer, HttpRequest& out);
// Percent-decoding; '+' becomes a space only when plusIsSpace (queries).
std::string urlDecode(const std::string& s, bool plusIsSpace);
bool hostAllowed(const std::string& host, int port);
bool originAllowed(const std::string& origin, int port);
const char* statusText(int status);
// Content type for a file name by extension (html, js, css, gif, ...).
std::string mimeType(const std::filesystem::path& file);

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer(EventLoop& loop, Handler handler);
    ~HttpServer();

    // Binds 127.0.0.1:port. False (and logged) when the port is taken.
    bool listen(int port);
    int port() const { return port_; }

private:
    struct Client;

    void acceptClient();
    void onReadable(int fd);
    void onWritable(int fd);
    void respond(int fd, Client& client, HttpResponse response);
    void watch(int fd, uint32_t events);
    void closeClient(int fd);

    EventLoop& loop_;
    Handler handler_;
    int listenFd_ = -1;
    int port_ = 0;
    std::map<int, std::unique_ptr<Client>> clients_;
};

}  // namespace canvas
