#include "ws_client.h"

#include <ztk/net/tcp_client.h>
#include <ztk/poller/poller.h>

#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <sstream>
#include <utility>

namespace ipc_mini::webrtc_net {
namespace {

bool parse_ws_url(const std::string& url, std::string& host, int& port,
                  std::string& path)
{
    const std::string prefix = "ws://";
    if (url.rfind(prefix, 0) != 0) {
        return false;
    }
    std::string rest = url.substr(prefix.size());
    path = "/";
    const auto slash = rest.find('/');
    std::string hostport = rest;
    if (slash != std::string::npos) {
        hostport = rest.substr(0, slash);
        path = rest.substr(slash);
    }
    const auto colon = hostport.rfind(':');
    if (colon == std::string::npos) {
        host = hostport;
        port = 80;
    } else {
        host = hostport.substr(0, colon);
        port = std::atoi(hostport.substr(colon + 1).c_str());
    }
    return !host.empty() && port > 0 && port <= 65535;
}

std::string b64(const uint8_t* data, std::size_t len)
{
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (std::size_t i = 0; i < len; i += 3) {
        const uint32_t value = (static_cast<uint32_t>(data[i]) << 16) |
            (i + 1 < len ? static_cast<uint32_t>(data[i + 1]) << 8 : 0) |
            (i + 2 < len ? static_cast<uint32_t>(data[i + 2]) : 0);
        out.push_back(table[(value >> 18) & 63]);
        out.push_back(table[(value >> 12) & 63]);
        out.push_back(i + 1 < len ? table[(value >> 6) & 63] : '=');
        out.push_back(i + 2 < len ? table[value & 63] : '=');
    }
    return out;
}

struct SyncCall {
    std::function<bool()> fn;
    std::mutex mutex;
    std::condition_variable cv;
    bool done{false};
    bool result{false};
};

void run_sync_call(void* user)
{
    auto* call = static_cast<SyncCall*>(user);
    const bool result = call && call->fn ? call->fn() : false;
    if (!call) {
        return;
    }
    {
        std::lock_guard lock(call->mutex);
        call->result = result;
        call->done = true;
    }
    call->cv.notify_one();
}

} // namespace

WsClient::WsClient(ztk_poller* poller) : poller_(poller)
{
}

WsClient::~WsClient()
{
    close();
}

void WsClient::set_message_handler(MessageHandler handler)
{
    std::lock_guard lock(handler_mutex_);
    on_message_ = std::move(handler);
}

void WsClient::set_state_handler(StateHandler handler)
{
    std::lock_guard lock(handler_mutex_);
    on_state_ = std::move(handler);
}

bool WsClient::call_on_poller(std::function<bool()> fn)
{
    if (!poller_ || !fn) {
        return false;
    }
    if (ztk_poller_is_current_thread(poller_)) {
        return fn();
    }

    SyncCall call;
    call.fn = std::move(fn);
    if (ztk_poller_async(poller_, &run_sync_call, &call, 0) != ZTK_OK) {
        return false;
    }
    std::unique_lock lock(call.mutex);
    call.cv.wait(lock, [&call] { return call.done; });
    return call.result;
}

bool WsClient::connect(const std::string& url)
{
    std::string host;
    std::string path;
    int port = 80;
    if (!parse_ws_url(url, host, port, path) || !poller_) {
        return false;
    }
    return call_on_poller([this, host = std::move(host),
                           path = std::move(path), port]() mutable {
        host_ = std::move(host);
        path_ = std::move(path);
        port_ = port;
        return connect_on_poller();
    });
}

bool WsClient::connect_on_poller()
{
    close_on_poller(false);

    static const ztk_tcp_client_ops_t ops{
        &WsClient::on_tcp_connect,
        &WsClient::on_tcp_recv,
        &WsClient::on_tcp_error,
    };
    ztk_tcp_client_opts_t options{};
    options.poller = poller_;
    options.ops = &ops;
    options.user = this;
    client_ = ztk_tcp_client_create(&options);
    if (!client_) {
        return false;
    }

    handshake_complete_ = false;
    handshake_buffer_.clear();
    frame_buffer_.clear();
    connected_ = false;
    if (ztk_tcp_client_connect(
            client_, host_.c_str(), static_cast<uint16_t>(port_)) != ZTK_OK) {
        ztk_tcp_client_destroy(client_);
        client_ = nullptr;
        return false;
    }
    return true;
}

void WsClient::on_tcp_connect(ztk_tcp_client*, void* user)
{
    auto* self = static_cast<WsClient*>(user);
    if (!self || !self->send_handshake()) {
        if (self) {
            self->close_on_poller(true);
        }
    }
}

void WsClient::on_tcp_recv(ztk_tcp_client*, const void* data,
                           std::size_t len, void* user)
{
    auto* self = static_cast<WsClient*>(user);
    if (self && data && len) {
        self->handle_tcp_data(static_cast<const uint8_t*>(data), len);
    }
}

void WsClient::on_tcp_error(ztk_tcp_client*, void* user)
{
    auto* self = static_cast<WsClient*>(user);
    if (!self) {
        return;
    }
    self->connected_ = false;
    self->handshake_complete_ = false;
    self->handshake_buffer_.clear();
    self->frame_buffer_.clear();
    self->notify_state(false);
}

bool WsClient::send_handshake()
{
    if (!client_) {
        return false;
    }
    uint8_t key_raw[16];
    std::random_device random;
    for (auto& byte : key_raw) {
        byte = static_cast<uint8_t>(random());
    }
    const std::string key = b64(key_raw, sizeof(key_raw));
    std::ostringstream request;
    request << "GET " << path_ << " HTTP/1.1\r\n"
            << "Host: " << host_ << ":" << port_ << "\r\n"
            << "Upgrade: websocket\r\n"
            << "Connection: Upgrade\r\n"
            << "Sec-WebSocket-Key: " << key << "\r\n"
            << "Sec-WebSocket-Version: 13\r\n\r\n";
    const std::string raw = request.str();
    return ztk_tcp_client_send(client_, raw.data(), raw.size()) == ZTK_OK;
}

void WsClient::handle_tcp_data(const uint8_t* data, std::size_t len)
{
    if (!handshake_complete_) {
        handshake_buffer_.append(
            reinterpret_cast<const char*>(data), len);
        if (handshake_buffer_.size() > 8192) {
            close_on_poller(true);
            return;
        }
        const auto end = handshake_buffer_.find("\r\n\r\n");
        if (end == std::string::npos) {
            return;
        }
        const std::string header = handshake_buffer_.substr(0, end + 4);
        if (header.find(" 101 ") == std::string::npos) {
            close_on_poller(true);
            return;
        }
        const std::size_t payload_offset = end + 4;
        frame_buffer_.insert(
            frame_buffer_.end(),
            handshake_buffer_.begin() +
                static_cast<std::ptrdiff_t>(payload_offset),
            handshake_buffer_.end());
        handshake_buffer_.clear();
        handshake_complete_ = true;
        connected_ = true;
        notify_state(true);
        consume_frames();
        return;
    }

    frame_buffer_.insert(frame_buffer_.end(), data, data + len);
    consume_frames();
}

void WsClient::consume_frames()
{
    while (frame_buffer_.size() >= 2) {
        const bool fin = (frame_buffer_[0] & 0x80) != 0;
        const uint8_t opcode = frame_buffer_[0] & 0x0f;
        const bool masked = (frame_buffer_[1] & 0x80) != 0;
        uint64_t payload_len = frame_buffer_[1] & 0x7f;
        std::size_t header = 2;
        if (payload_len == 126) {
            if (frame_buffer_.size() < 4) {
                return;
            }
            payload_len =
                (static_cast<uint64_t>(frame_buffer_[2]) << 8) |
                frame_buffer_[3];
            header = 4;
        } else if (payload_len == 127) {
            close_on_poller(true);
            return;
        }
        const std::size_t mask_len = masked ? 4 : 0;
        if (payload_len > 65535 ||
            frame_buffer_.size() < header + mask_len + payload_len) {
            return;
        }

        const uint8_t* mask =
            masked ? frame_buffer_.data() + header : nullptr;
        const uint8_t* payload =
            frame_buffer_.data() + header + mask_len;
        std::vector<uint8_t> decoded(static_cast<std::size_t>(payload_len));
        for (std::size_t i = 0; i < decoded.size(); ++i) {
            decoded[i] = payload[i] ^ (masked ? mask[i % 4] : 0);
        }
        frame_buffer_.erase(
            frame_buffer_.begin(),
            frame_buffer_.begin() +
                static_cast<std::ptrdiff_t>(header + mask_len + payload_len));

        if (opcode == 0x8) {
            close_on_poller(true);
            return;
        }
        if (opcode == 0x9) {
            (void)send_frame_on_poller(0x0a, decoded.data(), decoded.size());
            continue;
        }
        if ((opcode == 0x1 || opcode == 0x0) && fin) {
            MessageHandler handler;
            {
                std::lock_guard lock(handler_mutex_);
                handler = on_message_;
            }
            if (handler) {
                handler(std::string(decoded.begin(), decoded.end()));
            }
        }
    }
}

bool WsClient::send_frame_on_poller(uint8_t opcode, const uint8_t* data,
                                    std::size_t len)
{
    if (!client_ || !connected_.load() || len > 65535) {
        return false;
    }
    std::vector<uint8_t> frame;
    frame.reserve(len + 8);
    frame.push_back(static_cast<uint8_t>(0x80 | (opcode & 0x0f)));
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(0x80 | len));
    } else {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xff));
        frame.push_back(static_cast<uint8_t>(len & 0xff));
    }

    uint8_t mask[4];
    std::random_device random;
    for (auto& byte : mask) {
        byte = static_cast<uint8_t>(random());
    }
    frame.insert(frame.end(), mask, mask + 4);
    for (std::size_t i = 0; i < len; ++i) {
        frame.push_back(static_cast<uint8_t>(data[i] ^ mask[i % 4]));
    }
    return ztk_tcp_client_send(client_, frame.data(), frame.size()) == ZTK_OK;
}

bool WsClient::send_text_on_poller(const std::string& text)
{
    return send_frame_on_poller(
        0x01, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

bool WsClient::send_text(const std::string& text)
{
    if (!connected_.load() || text.empty()) {
        return false;
    }
    return call_on_poller(
        [this, text] { return send_text_on_poller(text); });
}

void WsClient::notify_state(bool connected)
{
    StateHandler state;
    {
        std::lock_guard lock(handler_mutex_);
        state = on_state_;
    }
    if (state) {
        state(connected);
    }
}

void WsClient::close_on_poller(bool notify)
{
    connected_ = false;
    handshake_complete_ = false;
    handshake_buffer_.clear();
    frame_buffer_.clear();
    if (client_) {
        ztk_tcp_client_destroy(client_);
        client_ = nullptr;
    }
    if (notify) {
        notify_state(false);
    }
}

void WsClient::close()
{
    if (!poller_) {
        return;
    }
    (void)call_on_poller([this] {
        close_on_poller(false);
        return true;
    });
}

} // namespace ipc_mini::webrtc_net
