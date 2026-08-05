#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct ztk_poller;
struct ztk_tcp_client;

namespace zero_mini::webrtc_net {

/**
 * Minimal client WebSocket (text frames, ws:// only).
 * Put TLS terminator (Nginx/SLB) in front on Aliyun for wss://.
 */
class WsClient final {
public:
    using MessageHandler = std::function<void(const std::string&)>;
    using StateHandler = std::function<void(bool connected)>;

    explicit WsClient(ztk_poller* poller);
    ~WsClient();

    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;

    bool connect(const std::string& url);
    void close();
    bool send_text(const std::string& text);
    bool connected() const { return connected_.load(); }

    void set_message_handler(MessageHandler handler);
    void set_state_handler(StateHandler handler);

private:
    static void on_tcp_connect(ztk_tcp_client* client, void* user);
    static void on_tcp_recv(ztk_tcp_client* client, const void* data,
                            std::size_t len, void* user);
    static void on_tcp_error(ztk_tcp_client* client, void* user);

    bool call_on_poller(std::function<bool()> fn);
    bool connect_on_poller();
    void close_on_poller(bool notify);
    bool send_text_on_poller(const std::string& text);
    bool send_frame_on_poller(uint8_t opcode, const uint8_t* data,
                              std::size_t len);
    bool send_handshake();
    void handle_tcp_data(const uint8_t* data, std::size_t len);
    void consume_frames();
    void notify_state(bool connected);

    ztk_poller* poller_{nullptr};
    ztk_tcp_client* client_{nullptr};
    std::string host_;
    std::string path_;
    int port_{80};
    std::string handshake_buffer_;
    std::vector<uint8_t> frame_buffer_;
    bool handshake_complete_{false};
    std::atomic<bool> connected_{false};
    MessageHandler on_message_;
    StateHandler on_state_;
    std::mutex handler_mutex_;
};

} // namespace zero_mini::webrtc_net
