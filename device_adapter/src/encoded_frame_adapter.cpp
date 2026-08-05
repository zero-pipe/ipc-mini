#include "encoded_frame_adapter.h"

#include <cstring>
#include <limits>

namespace zero_ipc::device_adapter {

std::shared_ptr<media::MediaFrame> copy_encoded_frame(const EncodedFrameView& source)
{
    const bool video_codec = source.codec == media::Codec::H264 || source.codec == media::Codec::H265;
    const bool audio_codec = source.codec == media::Codec::AAC || source.codec == media::Codec::G711A ||
                             source.codec == media::Codec::G711U;
    if ((source.type == media::MediaType::Video && !video_codec) ||
        (source.type == media::MediaType::Audio && !audio_codec)) {
        return nullptr;
    }

    std::size_t total = 0;
    for (const auto& chunk : source.chunks) {
        if (!chunk.data || chunk.size == 0 ||
            chunk.size > std::numeric_limits<std::size_t>::max() - total) {
            return nullptr;
        }
        total += chunk.size;
    }
    if (total == 0) {
        return nullptr;
    }

    auto frame = media::MediaFrame::create(source.type, source.codec,
                                            source.stream_id, total);
    if (!frame) {
        return nullptr;
    }
    frame->set_kind(source.kind);
    frame->set_timestamps(source.pts_ms, source.dts_ms);
    if (source.type == media::MediaType::Video) {
        frame->reserve_nalus(source.chunks.size());
    }

    std::size_t offset = 0;
    for (const auto& chunk : source.chunks) {
        std::memcpy(frame->data() + offset, chunk.data, chunk.size);
        if (source.type == media::MediaType::Video) {
            frame->add_nalu(offset, chunk.size);
        }
        offset += chunk.size;
    }
    return frame;
}

} // namespace zero_ipc::device_adapter
