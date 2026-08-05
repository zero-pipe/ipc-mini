#include "packaging/mpeg_muxer.h"

#include <mpeg-muxer.h>
#include <mpeg-proto.h>
#include <mpeg4-aac.h>
#include <cstdlib>
#include <limits>
#include <utility>

namespace zero_ipc::packaging {

namespace {
int stream_codec(media::Codec codec)
{
    switch (codec) {
    case media::Codec::H264: return PSI_STREAM_H264;
    case media::Codec::H265: return PSI_STREAM_H265;
    case media::Codec::AAC: return PSI_STREAM_AAC;
    case media::Codec::G711A: return PSI_STREAM_AUDIO_G711A;
    case media::Codec::G711U: return PSI_STREAM_AUDIO_G711U;
    default: return PSI_STREAM_RESERVED;
    }
}

bool is_video_codec(media::Codec codec)
{
    return codec == media::Codec::H264 || codec == media::Codec::H265;
}

bool milliseconds_to_90khz(int64_t value, int64_t& result)
{
    if (value < 0 || value > std::numeric_limits<int64_t>::max() / 90) {
        return false;
    }
    result = value * 90;
    return true;
}

bool is_adts(const media::MediaFrame& frame)
{
    if (frame.size() < 7) {
        return false;
    }
    mpeg4_aac_t aac{};
    return mpeg4_aac_adts_load(frame.data(), frame.size(), &aac) >= 7;
}
}

struct MpegMuxer::Context {
    ~Context()
    {
        if (muxer) {
            mpeg_muxer_destroy(muxer);
        }
    }

    MuxedDataSink* output{nullptr};
    mpeg_muxer_t* muxer{nullptr};
    media::Codec video_codec{media::Codec::Unknown};
    media::Codec audio_codec{media::Codec::Unknown};
    int video_stream{-1};
    int audio_stream{-1};
    bool output_failed{false};
};

MpegMuxer::MpegMuxer(MuxedDataSink output, MpegContainer container)
    : output_(std::move(output)), context_(std::make_unique<Context>())
{
    context_->output = &output_;
    mpeg_muxer_func_t callbacks{
        &MpegMuxer::allocate_packet,
        &MpegMuxer::free_packet,
        &MpegMuxer::write_packet,
    };
    context_->muxer = mpeg_muxer_create(
        container == MpegContainer::ProgramStream ? 1 : 0,
        &callbacks, context_.get());
}

MpegMuxer::~MpegMuxer() = default;

bool MpegMuxer::add_track(media::Codec codec)
{
    if (!context_ || !context_->muxer || stream_codec(codec) == PSI_STREAM_RESERVED) {
        return false;
    }
    const bool video = is_video_codec(codec);
    if ((video && context_->video_stream > 0) || (!video && context_->audio_stream > 0)) {
        return false;
    }
    const int id = mpeg_muxer_add_stream(context_->muxer, stream_codec(codec), nullptr, 0);
    if (id <= 0) {
        return false;
    }
    if (video) {
        context_->video_stream = id;
        context_->video_codec = codec;
    } else {
        context_->audio_stream = id;
        context_->audio_codec = codec;
    }
    return true;
}

bool MpegMuxer::write_frame(const std::shared_ptr<const media::MediaFrame>& frame)
{
    if (!frame || !context_ || !context_->muxer || context_->output_failed || frame->size() == 0) {
        return false;
    }
    const bool video = frame->type() == media::MediaType::Video;
    const int stream = video ? context_->video_stream : context_->audio_stream;
    const media::Codec codec = video ? context_->video_codec : context_->audio_codec;
    if (stream <= 0 || frame->codec() != codec ||
        (codec == media::Codec::AAC && !is_adts(*frame))) {
        return false;
    }
    int64_t pts = 0;
    int64_t dts = 0;
    if (!milliseconds_to_90khz(frame->pts_ms(), pts) ||
        !milliseconds_to_90khz(frame->dts_ms(), dts)) {
        return false;
    }
    const int flags = frame->keyframe() ? MPEG_FLAG_IDR_FRAME : 0;
    return mpeg_muxer_input(context_->muxer, stream, flags, pts, dts,
                            frame->data(), frame->size()) == 0;
}

void MpegMuxer::close()
{
    if (context_ && context_->muxer) {
        mpeg_muxer_destroy(context_->muxer);
        context_->muxer = nullptr;
    }
}

void* MpegMuxer::allocate_packet(void*, std::size_t bytes)
{
    return std::malloc(bytes);
}

void MpegMuxer::free_packet(void*, void* packet)
{
    std::free(packet);
}

int MpegMuxer::write_packet(void* param, int, void* packet, std::size_t bytes)
{
    auto* context = static_cast<Context*>(param);
    if (!context || !context->output || !*context->output) {
        return -1;
    }
    try {
        if ((*context->output)(static_cast<const uint8_t*>(packet), bytes)) {
            return 0;
        }
    } catch (...) {
    }
    context->output_failed = true;
    return -1;
}

} // namespace zero_ipc::packaging
