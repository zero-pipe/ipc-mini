#pragma once

#include <cstdint>
#include <vector>

namespace zero_ipc::media {

enum class MediaType : uint8_t {
    Video,
    Audio,
};

enum class Codec : uint8_t {
    Unknown,
    H264,
    H265,
    AAC,
    G711A,
    G711U,
};

enum class FrameKind : uint8_t {
    Unknown,
    Key,
    Inter,
    Audio,
};

struct VideoFormat {
    Codec codec{Codec::Unknown};
    int width{0};
    int height{0};
    int frame_rate{0};
    std::vector<uint8_t> extradata;
};

struct AudioFormat {
    Codec codec{Codec::Unknown};
    int sample_rate{0};
    int channels{0};
    int bits_per_sample{0};
    std::vector<uint8_t> extradata;
};

} // namespace zero_ipc::media
