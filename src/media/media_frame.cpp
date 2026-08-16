#include "media/media_frame.h"

namespace ipc_mini::media {

std::shared_ptr<MediaFrame> MediaFrame::create(MediaType type, Codec codec,
                                                int stream_id,
                                                std::size_t payload_size)
{
    return std::shared_ptr<MediaFrame>(
        new MediaFrame(type, codec, stream_id, payload_size));
}

MediaFrame::MediaFrame(MediaType type, Codec codec, int stream_id,
                       std::size_t payload_size)
    : type_(type), codec_(codec), stream_id_(stream_id),
      payload_(payload_size)
{
}

void MediaFrame::set_timestamps(int64_t pts_ms, int64_t dts_ms) noexcept
{
    pts_ms_ = pts_ms;
    dts_ms_ = dts_ms;
}

void MediaFrame::add_nalu(std::size_t offset, std::size_t size)
{
    if (offset > payload_.size() || size > payload_.size() - offset) {
        return;
    }
    nalus_.push_back({offset, size});
}

} // namespace ipc_mini::media
