#include "encoded_stream_channel.h"

#include <algorithm>
#include <cstdio>

namespace zero_ipc::device_adapter {

namespace {
media::Codec codec_from_mode(const std::string& mode)
{
    return mode.find("H265") != std::string::npos ? media::Codec::H265 : media::Codec::H264;
}

media::FrameKind kind_from_type(uint32_t type, media::Codec codec,
                                 const zero_ipc::util::stream_head& head)
{
    /* STREAM_* are preprocessor macros, not namespaced enumerators. */
    if (type == STREAM_AUDIO_FRAME) {
        return media::FrameKind::Audio;
    }
    if (type == STREAM_I_FRAME) {
        return media::FrameKind::Key;
    }
    if (type == STREAM_NALU_SLICE) {
        const uint32_t count = std::min<uint32_t>(head.nalu_count, MAX_STREAM_NALU_COUNT);
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* data = head.nalu[i].data;
            const std::size_t size = head.nalu[i].size;
            if (!data || size < 5) {
                continue;
            }
            /* HiSilicon packs Annex-B: 00 00 00 01 or 00 00 01 */
            std::size_t offset = 0;
            if (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1) {
                offset = 4;
            } else if (size >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1) {
                offset = 3;
            } else {
                offset = 0;
            }
            if (size <= offset) {
                continue;
            }
            const uint8_t type_value = codec == media::Codec::H265
                ? static_cast<uint8_t>((data[offset] >> 1) & 0x3f)
                : static_cast<uint8_t>(data[offset] & 0x1f);
            if ((codec == media::Codec::H264 && type_value == 5) ||
                (codec == media::Codec::H265 && (type_value == 19 || type_value == 20 || type_value == 21))) {
                return media::FrameKind::Key;
            }
        }
    }
    /*
     * P / non-IDR NALU_SLICE → Inter (forward predicted).
     * STREAM_B_FRAME → Inter for now (no FrameKind::B yet). Keep this branch:
     * current VENC (NORMAL_P + Baseline) does not emit B, but the tag must stay
     * for a future GOP/profile that enables B frames.
     */
    if (type == STREAM_P_FRAME || type == STREAM_B_FRAME || type == STREAM_NALU_SLICE) {
        return media::FrameKind::Inter;
    }
    return media::FrameKind::Unknown;
}
} // namespace

EncodedStreamChannel::EncodedStreamChannel(
    const char* sensor_name, const char* encoder_mode, int32_t channel_id,
    std::shared_ptr<media::MediaSource> media_source)
    : hisilicon::dev::chn(sensor_name, encoder_mode, channel_id),
      media_source_(std::move(media_source)),
      encoder_mode_(encoder_mode ? encoder_mode : "")
{
}

bool EncodedStreamChannel::register_video_tracks(
    int width, int height, int frame_rate, media::Codec video_codec)
{
    if (!media_source_ || width <= 0 || height <= 0 || frame_rate <= 0) {
        return false;
    }
    for (int stream = MAIN_STREAM_ID; stream <= SUB_STREAM_ID; ++stream) {
        media::StreamTrack track;
        track.stream_id = stream;
        track.type = media::MediaType::Video;
        track.video.codec = video_codec;
        track.video.width = stream == MAIN_STREAM_ID ? width : 720;
        track.video.height = stream == MAIN_STREAM_ID ? height : 480;
        track.video.frame_rate = frame_rate;
        track.clock_rate = 90000;
        media_source_->set_track(std::move(track));
    }
    return true;
}

bool EncodedStreamChannel::register_audio_tracks(
    media::Codec audio_codec, int sample_rate,
    int channels, int bits_per_sample)
{
    if (!media_source_ || sample_rate <= 0 || channels <= 0 ||
        bits_per_sample <= 0 ||
        (audio_codec != media::Codec::G711U &&
         audio_codec != media::Codec::AAC)) {
        return false;
    }
    for (int stream = MAIN_STREAM_ID; stream <= SUB_STREAM_ID; ++stream) {
        media::StreamTrack track;
        track.stream_id = stream;
        track.type = media::MediaType::Audio;
        track.audio.codec = audio_codec;
        track.audio.sample_rate = sample_rate;
        track.audio.channels = channels;
        track.audio.bits_per_sample = bits_per_sample;
        track.clock_rate = sample_rate;
        media_source_->set_track(std::move(track));
    }
    audio_codec_ = audio_codec;
    return true;
}

void EncodedStreamChannel::on_stream_come(zero_ipc::util::stream_obj_ptr sobj,
                                          zero_ipc::util::stream_head* head,
                                          const char* buf, int32_t len)
{
    /* HiSilicon venc callback stack is tiny — no printf/chrono/heavy work here. */
    if (!media_source_ || !sobj || !head) {
        return;
    }
    EncodedFrameView frame_view;
    frame_view.stream_id = sobj->stream_id();
    frame_view.codec = head->type == STREAM_AUDIO_FRAME
        ? audio_codec_ : codec_from_mode(encoder_mode_);
    frame_view.kind = kind_from_type(head->type, frame_view.codec, *head);
    frame_view.type = frame_view.kind == media::FrameKind::Audio
        ? media::MediaType::Audio : media::MediaType::Video;
    if (frame_view.type == media::MediaType::Audio) {
        if (!buf || len <= 0 ||
            frame_view.codec == media::Codec::Unknown) {
            return;
        }
        frame_view.pts_ms = head->time_stamp;
        frame_view.dts_ms = head->time_stamp;
        frame_view.chunks.push_back(
            {reinterpret_cast<const uint8_t*>(buf), static_cast<std::size_t>(len)});
    } else {
        const auto count = std::min<uint32_t>(head->nalu_count, MAX_STREAM_NALU_COUNT);
        if (count == 0) {
            return;
        }
        /*
         * dev_venc.cpp stores the encoder PTS in every nalu[].time_stamp;
         * stream_head::time_stamp remains zero. Reading the latter made every
         * RTP timestamp zero, so ffmpeg waited out its probe window.
         */
        frame_view.pts_ms = static_cast<int64_t>(head->nalu[0].time_stamp);
        frame_view.dts_ms = frame_view.pts_ms;
        frame_view.chunks.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!head->nalu[i].data || head->nalu[i].size == 0) {
                return;
            }
            frame_view.chunks.push_back({head->nalu[i].data, head->nalu[i].size});
        }
    }

    auto frame = copy_encoded_frame(frame_view);
    if (frame) {
        media_source_->publish(std::move(frame));
    }
}

void EncodedStreamChannel::on_stream_error(zero_ipc::util::stream_obj_ptr, int32_t)
{
}

} // namespace zero_ipc::device_adapter
