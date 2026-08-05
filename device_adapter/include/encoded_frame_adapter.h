#pragma once

#include "media/media_frame.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace zero_ipc::device_adapter {

struct EncodedChunkView {
    const uint8_t* data{nullptr};
    std::size_t size{0};
};

struct EncodedFrameView {
    media::MediaType type{media::MediaType::Video};
    media::Codec codec{media::Codec::Unknown};
    media::FrameKind kind{media::FrameKind::Unknown};
    int stream_id{0};
    int64_t pts_ms{0};
    int64_t dts_ms{0};
    std::vector<EncodedChunkView> chunks;
};

std::shared_ptr<media::MediaFrame> copy_encoded_frame(const EncodedFrameView& source);

} // namespace zero_ipc::device_adapter
