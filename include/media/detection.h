#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ipc_mini::media {

struct DetectionBox {
    int class_id{0};
    float score{0.f};
    /** Normalized to preview frame [0,1]. */
    float x{0.f};
    float y{0.f};
    float w{0.f};
    float h{0.f};
    std::string label;
};

struct DetectionResult {
    int64_t pts_ms{0};
    int frame_width{0};
    int frame_height{0};
    std::vector<DetectionBox> boxes;
};

} // namespace ipc_mini::media
