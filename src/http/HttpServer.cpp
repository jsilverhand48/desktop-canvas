// Implementation of HttpServer declared in HttpServer.h. See the header for
// the scope and the Host / Origin rules.
#include "http/HttpServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

#include "core/EventLoop.h"
#include "core/Log.h"

namespace canvas {

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxClients = 64;
constexpr size_t kChunk = 64 * 1024;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

HttpResponse HttpResponse::json(int status, std::string body) {
    HttpResponse r;
    r.status = status;
    r.body = std::move(body);
    return r;
}

HttpResponse HttpResponse::text(int status, std::string body) {
    HttpResponse r;
    r.status = status;
    r.contentType = "text/plain; charset=utf-8";
    r.body = std::move(body);
    return r;
}

std::string urlDecode(const std::string& s, bool plusIsSpace) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hexValue(s[i + 1]);
            int lo = hexValue(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        out.push_back(plusIsSpace && s[i] == '+' ? ' ' : s[i]);
    }
    return out;
}

ParseStatus parseRequest(const std::string& buffer, HttpRequest& out) {
    size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return buffer.size() > kMaxHeader ? ParseStatus::TooLarge
                                          : ParseStatus::Incomplete;
    if (headerEnd > kMaxHeader) return ParseStatus::TooLarge;

    std::istringstream lines(buffer.substr(0, headerEnd));
    std::string line;
    if (!std::getline(lines, line)) return ParseStatus::Bad;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    // Request line: METHOD SP target SP HTTP/1.x
    std::istringstream first(line);
    std::string method, target, version;
    if (!(first >> method >> target >> version)) return ParseStatus::Bad;
    if (version.rfind("HTTP/1.", 0) != 0 || target.empty() || target[0] != '/')
        return ParseStatus::Bad;
    for (char c : method)
        if (!std::isupper(static_cast<unsigned char>(c))) return ParseStatus::Bad;

    HttpRequest req;
    req.method = method;
    size_t q = target.find('?');
    req.path = urlDecode(target.substr(0, q), false);
    if (q != std::string::npos) {
        std::string query = target.substr(q + 1);
        size_t pos = 0;
        while (pos <= query.size()) {
            size_t amp = query.find('&', pos);
            std::string pair = query.substr(pos, amp == std::string::npos
                                                     ? std::string::npos
                                                     : amp - pos);
            if (!pair.empty()) {
                size_t eq = pair.find('=');
                std::string key = urlDecode(pair.substr(0, eq), true);
                std::string value = eq == std::string::npos
                                        ? ""
                                        : urlDecode(pair.substr(eq + 1), true);
                req.query[key] = value;
            }
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
    }

    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0) return ParseStatus::Bad;
        req.headers[lower(trim(line.substr(0, colon)))] =
            trim(line.substr(colon + 1));
    }
    if (req.headers.count("transfer-encoding")) return ParseStatus::Bad;

    size_t length = 0;
    auto cl = req.headers.find("content-length");
    if (cl != req.headers.end()) {
        if (cl->second.empty() ||
            !std::all_of(cl->second.begin(), cl->second.end(),
                         [](unsigned char c) { return std::isdigit(c); }) ||
            cl->second.size() > 9)
            return ParseStatus::Bad;
        length = std::stoul(cl->second);
        if (length > kMaxBody) return ParseStatus::TooLarge;
    }
    size_t bodyStart = headerEnd + 4;
    if (buffer.size() < bodyStart + length) return ParseStatus::Incomplete;
    req.body = buffer.substr(bodyStart, length);
    out = std::move(req);
    return ParseStatus::Complete;
}

bool hostAllowed(const std::string& host, int port) {
    std::string p = ":" + std::to_string(port);
    std::string h = lower(host);
    return h == "127.0.0.1" + p || h == "localhost" + p;
}

bool originAllowed(const std::string& origin, int port) {
    std::string p = ":" + std::to_string(port);
    std::string o = lower(origin);
    return o == "http://127.0.0.1" + p || o == "http://localhost" + p;
}

const char* statusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
    }
    return "Unknown";
}

std::string mimeType(const fs::path& file) {
    static const std::map<std::string, std::string> types = {
        {".html", "text/html; charset=utf-8"},
        {".js", "text/javascript; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".json", "application/json"},
        {".svg", "image/svg+xml"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".webp", "image/webp"},
        {".mp4", "video/mp4"},
        {".webm", "video/webm"}};
    auto it = types.find(lower(file.extension().string()));
    return it != types.end() ? it->second : "application/octet-stream";
}

struct HttpServer::Client {
    std::string in;
    std::string out;       // pending bytes (headers, body, file chunks)
    std::ifstream file;    // open while a file body is being streamed
    bool responding = false;
    uint64_t idleTimer = 0;
};

HttpServer::HttpServer(EventLoop& loop, Handler handler)
    : loop_(loop), handler_(std::move(handler)) {}

HttpServer::~HttpServer() {
    while (!clients_.empty()) closeClient(clients_.begin()->first);
    if (listenFd_ >= 0) {
        loop_.removeFd(listenFd_);
        close(listenFd_);
    }
}

bool HttpServer::listen(int port) {
    listenFd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) {
        log::error() << "ui socket: " << std::strerror(errno);
        return false;
    }
    int one = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1 only
    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(listenFd_, 16) < 0) {
        log::error() << "ui cannot listen on 127.0.0.1:" << port << ": "
                     << std::strerror(errno);
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    port_ = port;
    loop_.addFd(listenFd_, EPOLLIN, [this](uint32_t) { acceptClient(); });
    log::info() << "settings UI at http://127.0.0.1:" << port << "/";
    return true;
}

void HttpServer::acceptClient() {
    while (true) {
        int fd = accept4(listenFd_, nullptr, nullptr,
                         SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;  // EAGAIN: drained
        if (clients_.size() >= kMaxClients) {
            close(fd);
            continue;
        }
        auto client = std::make_unique<Client>();
        client->idleTimer =
            loop_.addTimer(kIdleMs, [this, fd] {
                auto it = clients_.find(fd);
                if (it != clients_.end()) it->second->idleTimer = 0;
                closeClient(fd);
            });
        clients_[fd] = std::move(client);
        watch(fd, EPOLLIN);
    }
}

void HttpServer::watch(int fd, uint32_t events) {
    loop_.removeFd(fd);
    loop_.addFd(fd, events | EPOLLRDHUP, [this, fd](uint32_t ready) {
        if (ready & (EPOLLERR | EPOLLHUP)) {
            closeClient(fd);
            return;
        }
        if (ready & EPOLLIN) onReadable(fd);
        if (clients_.count(fd) && (ready & EPOLLOUT)) onWritable(fd);
    });
}

void HttpServer::onReadable(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    Client& client = *it->second;
    if (client.responding) return;
    char buf[8192];
    while (true) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            client.in.append(buf, static_cast<size_t>(n));
            if (client.in.size() > kMaxHeader + kMaxBody + 4) break;
            continue;
        }
        if (n == 0) {  // peer closed before a full request
            closeClient(fd);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        closeClient(fd);
        return;
    }

    HttpRequest req;
    switch (parseRequest(client.in, req)) {
        case ParseStatus::Incomplete:
            return;
        case ParseStatus::Bad:
            respond(fd, client, HttpResponse::text(400, "bad request\n"));
            return;
        case ParseStatus::TooLarge:
            respond(fd, client, HttpResponse::text(413, "request too large\n"));
            return;
        case ParseStatus::Complete:
            break;
    }
    if (!hostAllowed(req.headers["host"], port_)) {
        respond(fd, client, HttpResponse::text(403, "forbidden host\n"));
        return;
    }
    bool mutating = req.method != "GET" && req.method != "HEAD";
    auto origin = req.headers.find("origin");
    if (mutating && origin != req.headers.end() &&
        !originAllowed(origin->second, port_)) {
        respond(fd, client, HttpResponse::text(403, "forbidden origin\n"));
        return;
    }
    HttpResponse response;
    try {
        response = handler_(req);
    } catch (const std::exception& e) {
        log::error() << "ui handler " << req.path << ": " << e.what();
        response = HttpResponse::json(500, R"({"error": "internal error"})");
    }
    if (req.method == "HEAD") {
        response.body.clear();
        response.file.clear();
    }
    respond(fd, client, std::move(response));
}

void HttpServer::respond(int fd, Client& client, HttpResponse response) {
    client.responding = true;
    uintmax_t length = response.body.size();
    if (!response.file.empty()) {
        std::error_code ec;
        length = fs::file_size(response.file, ec);
        client.file.open(response.file, std::ios::binary);
        if (ec || !client.file) {
            client.file = std::ifstream();
            response = HttpResponse::text(404, "not found\n");
            length = response.body.size();
        }
    }
    std::ostringstream head;
    head << "HTTP/1.1 " << response.status << " "
         << statusText(response.status) << "\r\n"
         << "Content-Type: " << response.contentType << "\r\n"
         << "Content-Length: " << length << "\r\n"
         << "Connection: close\r\n"
         << "X-Content-Type-Options: nosniff\r\n"
         << "Referrer-Policy: no-referrer\r\n";
    for (const auto& [name, value] : response.headers)
        head << name << ": " << value << "\r\n";
    head << "\r\n";
    client.out = head.str();
    if (response.file.empty() || !client.file.is_open())
        client.out += response.body;

    watch(fd, EPOLLOUT);
    onWritable(fd);
}

void HttpServer::onWritable(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    Client& client = *it->second;
    while (true) {
        if (client.out.empty() && client.file.is_open()) {
            char buf[kChunk];
            client.file.read(buf, sizeof(buf));
            std::streamsize got = client.file.gcount();
            if (got > 0) client.out.assign(buf, static_cast<size_t>(got));
            if (got <= 0 || client.file.eof()) client.file.close();
        }
        if (client.out.empty()) {
            closeClient(fd);  // done
            return;
        }
        ssize_t n = send(fd, client.out.data(), client.out.size(), MSG_NOSIGNAL);
        if (n > 0) {
            client.out.erase(0, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        closeClient(fd);
        return;
    }
}

void HttpServer::closeClient(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    if (it->second->idleTimer) loop_.cancelTimer(it->second->idleTimer);
    loop_.removeFd(fd);
    close(fd);
    clients_.erase(it);
}

}  // namespace canvas
