#include "record/http_segment_uploader.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <chrono>
#include <thread>

namespace ipc_mini::record {
namespace {

constexpr std::size_t kMaxQueued = 8;
constexpr int kPutTimeoutSec = 30;
constexpr int kMaxRetries = 3;

struct ParsedUrl {
    std::string host;
    int port{80};
    std::string path{"/"};
};

bool parse_http_url(const std::string& url, ParsedUrl& out)
{
    static constexpr char kPrefix[] = "http://";
    if (url.rfind(kPrefix, 0) != 0) {
        return false;
    }
    std::string rest = url.substr(sizeof(kPrefix) - 1);
    const auto slash = rest.find('/');
    const std::string hostport =
        slash == std::string::npos ? rest : rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (out.path.empty()) {
        out.path = "/";
    }
    const auto colon = hostport.rfind(':');
    if (colon != std::string::npos &&
        hostport.find(']') == std::string::npos) {
        out.host = hostport.substr(0, colon);
        out.port = std::atoi(hostport.substr(colon + 1).c_str());
    } else {
        out.host = hostport;
        out.port = 80;
    }
    return !out.host.empty() && out.port > 0 && out.port <= 65535;
}

std::string join_url_path(const std::string& base, const std::string& key)
{
    if (key.empty()) {
        return base.empty() ? "/" : base;
    }
    if (base.empty() || base == "/") {
        return key.front() == '/' ? key : "/" + key;
    }
    if (base.back() == '/' && key.front() == '/') {
        return base + key.substr(1);
    }
    if (base.back() != '/' && key.front() != '/') {
        return base + "/" + key;
    }
    return base + key;
}

const char* content_type_for(const std::string& path)
{
    if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".m3u8") == 0) {
        return "application/vnd.apple.mpegurl";
    }
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".m4s") == 0) {
        return "video/iso.segment";
    }
    return "video/mp4";
}

bool send_all(int fd, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t n = ::send(fd, bytes + sent, size - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

int connect_http(const ParsedUrl& base)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* info = nullptr;
    const std::string port = std::to_string(base.port);
    if (getaddrinfo(base.host.c_str(), port.c_str(), &hints, &info) != 0 ||
        !info) {
        return -1;
    }

    int fd = -1;
    for (addrinfo* cur = info; cur; cur = cur->ai_next) {
        fd = static_cast<int>(::socket(cur->ai_family, cur->ai_socktype,
                                       cur->ai_protocol));
        if (fd < 0) {
            continue;
        }
        timeval timeout{};
        timeout.tv_sec = kPutTimeoutSec;
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (::connect(fd, cur->ai_addr, cur->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(info);
    return fd;
}

bool status_ok(int fd, bool accept_not_found)
{
    char response[256]{};
    const ssize_t got = ::recv(fd, response, sizeof(response) - 1, 0);
    if (got <= 0) {
        return false;
    }
    int status = 0;
    if (std::sscanf(response, "HTTP/%*s %d", &status) != 1) {
        return false;
    }
    if (status >= 200 && status < 300) {
        return true;
    }
    return accept_not_found && status == 404;
}

bool put_once(const ParsedUrl& base, const std::string& token,
              const std::string& local_path, const std::string& object_key)
{
    std::ifstream file(local_path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0);

    const std::string path = join_url_path(base.path, object_key);
    char header[1024];
    int header_len = std::snprintf(
        header, sizeof(header),
        "PUT %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n",
        path.c_str(), base.host.c_str(), base.port,
        content_type_for(object_key), size);
    if (header_len <= 0 || static_cast<std::size_t>(header_len) >= sizeof(header)) {
        return false;
    }
    std::string request(header, static_cast<std::size_t>(header_len));
    if (!token.empty()) {
        request += "X-Record-Token: " + token + "\r\n";
    }
    request += "\r\n";

    const int fd = connect_http(base);
    if (fd < 0) {
        return false;
    }

    bool ok = send_all(fd, request.data(), request.size());
    char chunk[8192];
    while (ok && file) {
        file.read(chunk, sizeof(chunk));
        const auto n = static_cast<std::size_t>(file.gcount());
        if (n == 0) {
            break;
        }
        ok = send_all(fd, chunk, n);
    }
    ok = ok && status_ok(fd, false);
    ::close(fd);
    return ok;
}

bool delete_once(const ParsedUrl& base, const std::string& token,
                 const std::string& object_key)
{
    const std::string path = join_url_path(base.path, object_key);
    char header[1024];
    int header_len = std::snprintf(
        header, sizeof(header),
        "DELETE %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n",
        path.c_str(), base.host.c_str(), base.port);
    if (header_len <= 0 ||
        static_cast<std::size_t>(header_len) >= sizeof(header)) {
        return false;
    }
    std::string request(header, static_cast<std::size_t>(header_len));
    if (!token.empty()) {
        request += "X-Record-Token: " + token + "\r\n";
    }
    request += "\r\n";

    const int fd = connect_http(base);
    if (fd < 0) {
        return false;
    }
    const bool ok = send_all(fd, request.data(), request.size()) &&
        status_ok(fd, true);
    ::close(fd);
    return ok;
}

} // namespace

HttpSegmentUploader::HttpSegmentUploader(std::string base_url, std::string token)
    : base_url_(std::move(base_url)), token_(std::move(token))
{
    worker_ = std::thread([this] { worker_loop(); });
}

HttpSegmentUploader::~HttpSegmentUploader()
{
    stop();
}

void HttpSegmentUploader::enqueue(const std::string& local_path,
                                  const std::string& object_key)
{
    if (local_path.empty() || object_key.empty() || stopping_.load()) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (queue_.size() >= kMaxQueued) {
        std::fprintf(stderr, "[record] upload queue full, drop %s\n",
                     object_key.c_str());
        return;
    }
    queue_.push_back(Item{local_path, object_key, false});
    cv_.notify_one();
}

void HttpSegmentUploader::enqueue_delete(const std::string& object_key)
{
    if (object_key.empty() || stopping_.load()) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (queue_.size() >= kMaxQueued) {
        std::fprintf(stderr, "[record] upload queue full, drop delete %s\n",
                     object_key.c_str());
        return;
    }
    queue_.push_back(Item{"", object_key, true});
    cv_.notify_one();
}

void HttpSegmentUploader::stop()
{
    stopping_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void HttpSegmentUploader::worker_loop()
{
    while (true) {
        Item item;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] {
                return stopping_.load() || !queue_.empty();
            });
            if (queue_.empty()) {
                if (stopping_.load()) {
                    break;
                }
                continue;
            }
            item = std::move(queue_.front());
            queue_.pop_front();
        }
        bool ok = false;
        for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
            ok = item.remove ? delete_object(item) : put_file(item);
            if (ok) {
                break;
            }
            std::fprintf(stderr, "[record] %s retry %d/%d %s\n",
                         item.remove ? "delete" : "upload", attempt,
                         kMaxRetries, item.object_key.c_str());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (ok) {
            std::printf("[record] %s %s\n",
                        item.remove ? "deleted" : "uploaded",
                        item.object_key.c_str());
        } else {
            std::fprintf(stderr, "[record] %s failed %s\n",
                         item.remove ? "delete" : "upload",
                         item.object_key.c_str());
        }
    }
}

bool HttpSegmentUploader::put_file(const Item& item) const
{
    ParsedUrl url;
    if (!parse_http_url(base_url_, url)) {
        std::fprintf(stderr,
                     "[record] upload_url must be http://host[:port]/path\n");
        return false;
    }
    return put_once(url, token_, item.local_path, item.object_key);
}

bool HttpSegmentUploader::delete_object(const Item& item) const
{
    ParsedUrl url;
    if (!parse_http_url(base_url_, url)) {
        return false;
    }
    return delete_once(url, token_, item.object_key);
}

} // namespace ipc_mini::record
