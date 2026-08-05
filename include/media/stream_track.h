#pragma once

#include "media_format.h"
#include <cstdint>
#include <vector>

namespace zero_ipc::media {

struct StreamTrack {
    int stream_id{0};
    MediaType type{MediaType::Video};
    Codec codec{Codec::Unknown};
    int width{0};
    int height{0};
    int frame_rate{0};
    int sample_rate{0};
    int channels{0};
    int bits_per_sample{0};
    int clock_rate{90000};
    std::vector<uint8_t> extradata;
};

} // namespace zero_ipc::media
