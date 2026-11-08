// http_server.h
// 极简本地 HTTP/1.1 服务（仅监听回环地址，供浏览器 UI 使用）
//
// 设计:
//   · 单线程 accept 循环 + 每连接串行处理，无并发写数据竞争
//   · 仅支持 GET/POST/PUT/DELETE，Content-Length 请求体
//   · Windows 走 Winsock2，Linux 走 POSIX socket，无第三方依赖
//   · 注意：本头文件须在包含 windows.h 之前包含（winsock2.h 顺序要求）

#ifndef CLIJUDGE_GUI_HTTP_SERVER_H
#define CLIJUDGE_GUI_HTTP_SERVER_H

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace clijudge {
namespace gui {

#ifdef _WIN32
using socket_type = SOCKET;
static const socket_type kInvalidSocket = INVALID_SOCKET;
inline void closeSocket(socket_type s) { closesocket(s); }
#else
using socket_type = int;
static const socket_type kInvalidSocket = -1;
inline void closeSocket(socket_type s) { ::close(s); }
#endif

struct HttpRequest {
    std::string method;
    std::string path;
    std::string rawQuery;
    std::string body;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> query;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::map<std::string, std::string> headers;
    std::string body;
};

inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

inline std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, e - b + 1);
}

inline std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < s.size()) {
            auto hex = [](char ch) -> int {
                if (ch >= '0' && ch <= '9') return ch - '0';
                ch = (char)std::tolower((unsigned char)ch);
                if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                return -1;
            };
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += (char)((hi << 4) | lo);
                i += 2;
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

inline void parseQuery(const std::string& raw, std::map<std::string, std::string>& out) {
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t amp = raw.find('&', pos);
        std::string kv = raw.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = kv.find('=');
        if (eq == std::string::npos) {
            if (!kv.empty()) out[urlDecode(kv)] = "";
        } else {
            out[urlDecode(kv.substr(0, eq))] = urlDecode(kv.substr(eq + 1));
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
}

inline const char* reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "OK";
    }
}

inline bool setRecvTimeout(socket_type s, int seconds) {
#ifdef _WIN32
    DWORD ms = (DWORD)seconds * 1000;
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms)) == 0;
#else
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

inline bool setReuseAddr(socket_type s) {
    int yes = 1;
    return setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes)) == 0;
}

inline int recvBytes(socket_type s, char* buf, int len) {
#ifdef _WIN32
    return (int)recv(s, buf, len, 0);
#else
    return (int)::recv(s, buf, len, 0);
#endif
}

inline bool sendAll(socket_type s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = (int)::send(s, data.data() + sent, (int)(data.size() - sent), 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

inline bool readRequest(socket_type s, HttpRequest& req) {
    std::string raw;
    raw.reserve(4096);
    char buf[8192];
    size_t headerEnd = std::string::npos;

    while (raw.size() < (size_t)1 << 20) {  // 请求头上限 1MB
        headerEnd = raw.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
        int n = recvBytes(s, buf, (int)sizeof(buf));
        if (n <= 0) return false;
        raw.append(buf, (size_t)n);
    }
    if (headerEnd == std::string::npos) return false;

    std::string head = raw.substr(0, headerEnd);
    std::string body = raw.substr(headerEnd + 4);

    // 请求行
    size_t lineEnd = head.find("\r\n");
    std::string requestLine = head.substr(0, lineEnd);
    std::istringstream rl(requestLine);
    std::string target, version;
    if (!(rl >> req.method >> target >> version)) return false;

    // 查询串
    size_t q = target.find('?');
    if (q != std::string::npos) {
        req.path = target.substr(0, q);
        req.rawQuery = target.substr(q + 1);
        parseQuery(req.rawQuery, req.query);
    } else {
        req.path = target;
    }

    // 头部
    size_t pos = (lineEnd == std::string::npos) ? head.size() : lineEnd + 2;
    while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        std::string line = head.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            req.headers[toLower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
        }
        if (e == std::string::npos) break;
        pos = e + 2;
    }

    size_t contentLength = 0;
    auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
        try { contentLength = (size_t)std::stoull(it->second); } catch (...) { contentLength = 0; }
    }
    if (contentLength > (size_t)64 << 20) return false;  // 请求体上限 64MB

    while (body.size() < contentLength) {
        int n = recvBytes(s, buf, (int)sizeof(buf));
        if (n <= 0) return false;
        body.append(buf, (size_t)n);
    }
    if (body.size() > contentLength) body.resize(contentLength);
    req.body = body;
    return true;
}

inline std::string serializeResponse(const HttpResponse& res) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << res.status << ' ' << reasonPhrase(res.status) << "\r\n";
    oss << "Content-Type: " << res.contentType << "\r\n";
    oss << "Content-Length: " << res.body.size() << "\r\n";
    oss << "Cache-Control: no-store\r\n";
    oss << "X-Content-Type-Options: nosniff\r\n";
    oss << "Connection: close\r\n";
    for (const auto& [k, v] : res.headers) oss << k << ": " << v << "\r\n";
    oss << "\r\n";
    oss << res.body;
    return oss.str();
}

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    ~HttpServer() {
        stop();
#ifdef _WIN32
        if (wsaReady_) WSACleanup();
#endif
    }

    // 返回实际监听端口；失败返回 -1
    int bindAndListen(const std::string& host, int port, int tries = 20) {
#ifdef _WIN32
        if (!wsaReady_) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
            wsaReady_ = true;
        }
#endif
        socket_type s = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s == kInvalidSocket) return -1;
        setReuseAddr(s);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }

        int actual = port;
        bool bound = false;
        for (int i = 0; i < tries; ++i) {
            addr.sin_port = htons((uint16_t)(port + i));
            if (::bind(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
                actual = port + i;
                bound = true;
                break;
            }
        }
        if (!bound || ::listen(s, 64) != 0) {
            closeSocket(s);
            return -1;
        }
        listenSocket_ = s;
        port_ = actual;
        return actual;
    }

    int port() const { return port_; }

    void serve(const Handler& handler, volatile bool* running) {
        while (running && *running) {
            socket_type client = ::accept(listenSocket_, nullptr, nullptr);
            if (client == kInvalidSocket) {
#ifdef _WIN32
                continue;
#else
                if (errno == EINTR) continue;
                break;
#endif
            }
            handleConnection(client, handler);
            closeSocket(client);
        }
    }

    void stop() {
        if (listenSocket_ != kInvalidSocket) {
            closeSocket(listenSocket_);
            listenSocket_ = kInvalidSocket;
        }
    }

private:
    void handleConnection(socket_type client, const Handler& handler) {
        setRecvTimeout(client, 20);
        HttpRequest req;
        if (!readRequest(client, req)) return;

        HttpResponse res;
        try {
            res = handler(req);
        } catch (const std::exception& e) {
            res.status = 500;
            res.contentType = "application/json; charset=utf-8";
            res.body = std::string("{\"error\":\"internal: ") + e.what() + "\"}";
        } catch (...) {
            res.status = 500;
            res.contentType = "application/json; charset=utf-8";
            res.body = "{\"error\":\"internal error\"}";
        }
        sendAll(client, serializeResponse(res));
    }

    socket_type listenSocket_ = kInvalidSocket;
    int port_ = 0;
#ifdef _WIN32
    bool wsaReady_ = false;
#endif
};

} // namespace gui
} // namespace clijudge

#endif // CLIJUDGE_GUI_HTTP_SERVER_H
