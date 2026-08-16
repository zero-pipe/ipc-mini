#pragma once

#include "config/stream_config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ipc_mini::config {

struct SensorConfig {
    std::string name{"SC4336P"};
    int lane_mode{0};
    int width{2560};
    int height{1440};
    int fps{30};
    int wdr{0};
};

struct AudioConfig {
    bool enable{false};
    bool microphone{false};
    std::string codec{"G711U"};
};

struct RecordConfig {
    bool enable{false};
    std::string file;
};

struct WebRtcIceServerConfig {
    std::string urls;
    std::string username;
    std::string credential;
};

struct WebRtcConfig {
    bool enable{true};
    std::string signaling_url{"ws://127.0.0.1:8089/ws"};
    std::string signaling_token;
    std::string room{"door-1"};
    std::string preview{"sub"};
    int preview_stream_id{1};
    bool send_detections{true};
    bool disable_twcc{true};
    int rolling_buffer_sec{1};
    int expected_bitrate_kbps{1000};
    int max_viewers{3};
    std::vector<WebRtcIceServerConfig> ice_servers;
};

struct RuntimeConfig {
    SensorConfig sensor;
    AudioConfig audio;
    StreamsConfig streams;
    RecordConfig record;
    WebRtcConfig webrtc;
};

bool load_runtime_config(const std::string& config_directory,
                         RuntimeConfig& config,
                         std::string& error);

void resolve_runtime_paths(RuntimeConfig& config,
                           const std::string& install_root);

std::string format_streams_summary(const RuntimeConfig& config);

} // namespace ipc_mini::config
