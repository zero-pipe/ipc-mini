#pragma once

#include "ws_client.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

struct ztk_poller;
struct ztk_poller_timer;

namespace zero_mini::webrtc_net {

struct SignalingConfig {
    std::string url{"ws://127.0.0.1:8089/ws"};
    std::string room{"door-1"};
    std::string role{"master"};
    std::string client_id;
    std::string token;
};

class SignalingClient final {
public:
    using JsonHandler = std::function<void(const std::string& type,
                                           const std::string& json)>;

    SignalingClient(SignalingConfig config, ztk_poller* poller);
    ~SignalingClient();

    bool start();
    void stop();
    bool send_raw(const std::string& json);
    bool send_join();
    bool send_offer(const std::string& sdp, const std::string& to);
    bool send_answer(const std::string& sdp, const std::string& to);
    bool send_candidate(const std::string& candidate, const std::string& sdp_mid,
                        int sdp_mline_index, const std::string& to);

    void set_handler(JsonHandler handler);
    std::string client_id() const;
    bool connected() const { return ws_.connected(); }

private:
    static uint64_t reconnect_timer_cb(void* user);
    void on_message(const std::string& text);
    void on_ws_state(bool connected);
    void schedule_reconnect();
    std::string build_url() const;

    SignalingConfig config_;
    WsClient ws_;
    ztk_poller* poller_{nullptr};
    ztk_poller_timer* reconnect_timer_{nullptr};
    unsigned reconnect_attempt_{0};
    std::atomic<bool> stopping_{false};
    JsonHandler handler_;
    mutable std::mutex mutex_;
    std::string assigned_id_;
};

} // namespace zero_mini::webrtc_net
