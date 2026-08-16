#include "ai_stream_publisher.h"

#include <algorithm>

namespace ipc_mini::device_adapter {
namespace {

media::FrameKind h264_kind(const ipc_mini::util::stream_head& head)
{
    const uint32_t count =
        std::min<uint32_t>(head.nalu_count, MAX_STREAM_NALU_COUNT);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* data = head.nalu[i].data;
        const std::size_t size = head.nalu[i].size;
        if (!data || size < 4) {
            continue;
        }
        std::size_t offset = 0;
        if (size >= 5 && data[0] == 0 && data[1] == 0 &&
            data[2] == 0 && data[3] == 1) {
            offset = 4;
        } else if (data[0] == 0 && data[1] == 0 && data[2] == 1) {
            offset = 3;
        }
        if (offset < size && (data[offset] & 0x1f) == 5) {
            return media::FrameKind::Key;
        }
    }
    return media::FrameKind::Inter;
}

} // namespace

AiStreamPublisher::AiStreamPublisher(
    std::shared_ptr<media::MediaSource> media_source)
    : media_source_(std::move(media_source))
{
}

bool AiStreamPublisher::register_track(int width, int height, int frame_rate)
{
    if (!media_source_ || width <= 0 || height <= 0 || frame_rate <= 0) {
        return false;
    }
    media::StreamTrack track;
    track.stream_id = 2;
    track.type = media::MediaType::Video;
    track.video.codec = media::Codec::H264;
    track.video.width = width;
    track.video.height = height;
    track.video.frame_rate = frame_rate;
    track.clock_rate = 90000;
    media_source_->set_track(std::move(track));
    return true;
}

void AiStreamPublisher::on_stream_come(
    ipc_mini::util::stream_obj_ptr stream,
    ipc_mini::util::stream_head* head,
    const char*, int32_t)
{
    if (!media_source_ || !stream || !head ||
        head->type != STREAM_NALU_SLICE) {
        return;
    }

    const uint32_t count =
        std::min<uint32_t>(head->nalu_count, MAX_STREAM_NALU_COUNT);
    if (count == 0) {
        return;
    }

    EncodedFrameView view;
    view.type = media::MediaType::Video;
    view.codec = media::Codec::H264;
    view.kind = h264_kind(*head);
    view.stream_id = stream->stream_id();
    view.pts_ms = static_cast<int64_t>(head->nalu[0].time_stamp);
    view.dts_ms = view.pts_ms;
    view.chunks.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (!head->nalu[i].data || head->nalu[i].size == 0) {
            return;
        }
        view.chunks.push_back(
            {head->nalu[i].data, head->nalu[i].size});
    }

    auto frame = copy_encoded_frame(view);
    if (frame) {
        media_source_->publish(std::move(frame));
    }
}

void AiStreamPublisher::on_stream_error(
    ipc_mini::util::stream_obj_ptr, int32_t)
{
}

} // namespace ipc_mini::device_adapter
