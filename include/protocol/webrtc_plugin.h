#pragma once

#include "core/protocol_plugin.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ipc_mini::protocol {

struct WebRtcIceServer {
    std::string urls;
    std::string username;
    std::string credential;
};

struct WebRtcPluginOptions {
    std::string signaling_url{"ws://127.0.0.1:8089/ws"};
    std::string signaling_token;
    std::string room{"door-1"};
    int preview_stream_id{1};
    bool detections_enabled{true};
    bool disable_twcc{true};
    int rolling_buffer_sec{1};
    int expected_bitrate_bps{1000 * 1000};
    /** Concurrent household viewers (each gets its own PeerConnection). */
    int max_viewers{3};
    std::vector<WebRtcIceServer> ice_servers;
};

/**
 * Device-side WebRTC Master plugin.
 * One encoded preview stream fan-out to up to N viewer PeerConnections.
 *
 * Owns three workers:
 * - signaling poller: WebSocket / Offer-Answer / session state
 * - media thread pool (1): drain queues and writeFrame
 * - cleanup thread pool (1): blocking PeerConnection teardown
 */
class WebRtcPlugin final : public core::IProtocolPlugin {
public:
    explicit WebRtcPlugin(WebRtcPluginOptions options);
    ~WebRtcPlugin() override;

    bool start(const core::ProtocolContext& context) override;
    void stop() override;
    bool stop_after_device() const override { return true; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ipc_mini::protocol
