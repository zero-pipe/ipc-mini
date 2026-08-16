#pragma once

#include "media/media_frame.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ipc_mini::device_adapter {

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

/** Deep-copy a non-owning encoder view into a MediaSource frame. */
std::shared_ptr<media::MediaFrame> copy_encoded_frame(
    const EncodedFrameView& source);

} // namespace ipc_mini::device_adapter
