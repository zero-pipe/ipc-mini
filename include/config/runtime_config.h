#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zero_ipc::config {

struct VideoInputConfig {
    std::string sensor_name{"SC4336P"};
    int lane_mode{0};
    int max_width{2560};
    int max_height{1440};
    int frame_rate{30};
    int wdr_mode{0};
};

struct VideoEncoderConfig {
    std::string mode{"H264_CBR"};
    int width{1920};
    int height{1080};
    int frame_rate{25};
    int bitrate_kbps{2048};
    int svc_enabled{0};
};

struct AudioEncoderConfig {
    bool enabled{false};
    bool microphone{false};
    std::string codec{"G711U"};
};

struct TimeOsdConfig {
    bool enabled{true};
    int x{32};
    int y{32};
    int font_size{32};
};

struct Yolov8Config {
    bool enabled{true};
    std::string config_file{"/opt/zero_mini/yolov8/acl.json"};
    std::string model_file{"/opt/zero_mini/yolov8/yolov8_new_rpn.om"};
};

struct WebRtcIceServerConfig {
    std::string urls;
    std::string username;
    std::string credential;
};

struct WebRtcConfig {
    bool enabled{true};
    std::string signaling_url{"ws://127.0.0.1:8089/ws"};
    std::string signaling_token;
    std::string room{"door-1"};
    int preview_stream_id{1};
    bool detections_enabled{true};
    bool disable_twcc{true};
    int rolling_buffer_sec{1};
    int expected_bitrate_kbps{1000};
    int max_viewers{3};
    std::vector<WebRtcIceServerConfig> ice_servers;
};

struct RuntimeConfig {
    VideoInputConfig video_input;
    VideoEncoderConfig video_encoder;
    AudioEncoderConfig audio_encoder;
    TimeOsdConfig time_osd;
    Yolov8Config yolov8;
    WebRtcConfig webrtc;
};

bool load_runtime_config(const std::string& config_directory,
                         RuntimeConfig& config,
                         std::string& error);

} // namespace zero_ipc::config
