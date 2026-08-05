#include "packaging/flv_muxer.h"

#include <flv-muxer.h>
#include <flv-proto.h>
#include <mpeg4-aac.h>
#include <limits>
#include <utility>

namespace zero_ipc::packaging {

struct FlvMuxer::Context {
    explicit Context(FlvTagSink output) : output(std::move(output))
    {
        muxer = flv_muxer_create(&FlvMuxer::on_packet, this);
    }

    ~Context()
    {
        if (muxer) {
            flv_muxer_destroy(muxer);
        }
    }

    FlvTagSink output;
    flv_muxer_t* muxer{nullptr};
    media::Codec video_codec{media::Codec::Unknown};
    media::Codec audio_codec{media::Codec::Unknown};
    bool output_failed{false};
};

FlvMuxer::FlvMuxer(FlvTagSink output)
    : context_(std::make_unique<Context>(std::move(output)))
{
}

FlvMuxer::~FlvMuxer() = default;

bool FlvMuxer::configure_video(const media::VideoFormat& format)
{
    if (!context_ || !context_->muxer || context_->video_codec != media::Codec::Unknown ||
        format.width <= 0 || format.height <= 0 || format.frame_rate <= 0 ||
        (format.codec != media::Codec::H264 && format.codec != media::Codec::H265)) {
        return false;
    }
    flv_metadata_t metadata{};
    metadata.videocodecid = format.codec == media::Codec::H265 ? FLV_VIDEO_H265 : FLV_VIDEO_H264;
    metadata.width = format.width;
    metadata.height = format.height;
    metadata.framerate = format.frame_rate;
    if (flv_muxer_metadata(context_->muxer, &metadata) != 0) {
        return false;
    }
    flv_muxer_set_enhanced_rtmp(
        context_->muxer, format.codec == media::Codec::H265 ? 1 : 0);
    context_->video_codec = format.codec;
    return true;
}

bool FlvMuxer::configure_audio(const media::AudioFormat& format)
{
    if (!context_ || !context_->muxer || context_->audio_codec != media::Codec::Unknown ||
        format.sample_rate <= 0 || format.channels <= 0 ||
        (format.codec != media::Codec::AAC && format.codec != media::Codec::G711A &&
         format.codec != media::Codec::G711U)) {
        return false;
    }
    flv_metadata_t metadata{};
    switch (format.codec) {
    case media::Codec::AAC: metadata.audiocodecid = FLV_AUDIO_AAC >> 4; break;
    case media::Codec::G711A: metadata.audiocodecid = FLV_AUDIO_G711A >> 4; break;
    case media::Codec::G711U: metadata.audiocodecid = FLV_AUDIO_G711U >> 4; break;
    default: return false;
    }
    metadata.audiosamplerate = format.sample_rate;
    metadata.audiosamplesize = format.bits_per_sample > 0 ? format.bits_per_sample : 16;
    metadata.stereo = format.channels > 1;
    if (flv_muxer_metadata(context_->muxer, &metadata) != 0) {
        return false;
    }
    context_->audio_codec = format.codec;
    return true;
}

bool FlvMuxer::write_frame(const std::shared_ptr<const media::MediaFrame>& frame)
{
    return write_frame(frame, 0);
}

bool FlvMuxer::write_frame(
    const std::shared_ptr<const media::MediaFrame>& frame,
    int64_t timestamp_base_ms)
{
    if (!frame || !context_ || !context_->muxer || context_->output_failed || frame->size() == 0 ||
        timestamp_base_ms < 0 || frame->pts_ms() < timestamp_base_ms ||
        frame->dts_ms() < timestamp_base_ms ||
        frame->pts_ms() - timestamp_base_ms >
            std::numeric_limits<uint32_t>::max() ||
        frame->dts_ms() - timestamp_base_ms >
            std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    const bool configured = frame->type() == media::MediaType::Video
        ? frame->codec() == context_->video_codec
        : frame->codec() == context_->audio_codec;
    if (!configured) {
        return false;
    }
    if (frame->codec() == media::Codec::AAC) {
        mpeg4_aac_t aac{};
        if (frame->size() < 7 || mpeg4_aac_adts_load(frame->data(), frame->size(), &aac) < 7) {
            return false;
        }
    }
    const auto pts =
        static_cast<uint32_t>(frame->pts_ms() - timestamp_base_ms);
    const auto dts =
        static_cast<uint32_t>(frame->dts_ms() - timestamp_base_ms);
    int result = -1;
    switch (frame->codec()) {
    case media::Codec::H264:
        result = flv_muxer_avc(context_->muxer, frame->data(), frame->size(), pts, dts);
        break;
    case media::Codec::H265:
        result = flv_muxer_hevc(context_->muxer, frame->data(), frame->size(), pts, dts);
        break;
    case media::Codec::G711A:
        result = flv_muxer_g711a(context_->muxer, frame->data(), frame->size(), pts, dts);
        break;
    case media::Codec::G711U:
        result = flv_muxer_g711u(context_->muxer, frame->data(), frame->size(), pts, dts);
        break;
    case media::Codec::AAC:
        result = flv_muxer_aac(context_->muxer, frame->data(), frame->size(), pts, dts);
        break;
    default:
        return false;
    }
    return result == 0;
}

void FlvMuxer::reset()
{
    if (context_ && context_->muxer) {
        flv_muxer_reset(context_->muxer);
    }
}

int FlvMuxer::on_packet(void* param, int type, const void* data,
                        std::size_t bytes, uint32_t timestamp)
{
    auto* context = static_cast<Context*>(param);
    if (!context || !context->output) {
        return -1;
    }
    try {
        if (context->output(static_cast<FlvTagType>(type),
                            static_cast<const uint8_t*>(data), bytes,
                            timestamp)) {
            return 0;
        }
    } catch (...) {
    }
    context->output_failed = true;
    return -1;
}

} // namespace zero_ipc::packaging
