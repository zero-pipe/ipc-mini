#pragma once

#include <string>

namespace zero_ipc::config {

enum class StreamStart { Boot, OnDemand };

inline const char* stream_start_name(StreamStart start)
{
    return start == StreamStart::Boot ? "boot" : "on_demand";
}

inline int stream_id_from_name(const std::string& name)
{
    if (name == "main") {
        return 0;
    }
    if (name == "sub") {
        return 1;
    }
    if (name == "ai") {
        return 2;
    }
    return -1;
}

struct TimeOsdConfig {
    bool enable{false};
    int x{32};
    int y{32};
    int font_size{32};
};

struct EncodedStreamConfig {
    bool enable{true};
    StreamStart start{StreamStart::Boot};
    std::string mode{"H264_CBR"};
    int width{1280};
    int height{720};
    int fps{15};
    int bitrate_kbps{800};
    bool svc{false};
    TimeOsdConfig osd;
};

struct AiStreamConfig {
    bool enable{false};
    StreamStart start{StreamStart::OnDemand};
    std::string engine{"yolov8"};
    std::string model;
    std::string acl;
    int width{640};
    int height{640};
    int fps{1};
    int idle_stop_sec{5};
};

struct StreamsConfig {
    EncodedStreamConfig main;
    EncodedStreamConfig sub;
    AiStreamConfig ai;
};

} // namespace zero_ipc::config
