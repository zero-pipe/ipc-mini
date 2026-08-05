#include "packaging/mp4_muxer.h"

#include <mov-format.h>
#include <mov-writer.h>
#include <mpeg4-avc.h>
#include <mpeg4-hevc.h>
#include <mpeg4-aac.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace zero_ipc::packaging {

namespace {

struct MemoryFile {
    explicit MemoryFile(std::size_t max_bytes) : max_bytes(max_bytes) {}

    std::vector<uint8_t> bytes;
    std::size_t position{0};
    std::size_t max_bytes{0};
};

int buffer_read(void* param, void* data, uint64_t bytes)
{
    auto* file = static_cast<MemoryFile*>(param);
    if (!file || bytes > file->bytes.size() - std::min(file->position, file->bytes.size())) {
        return -1;
    }
    std::memcpy(data, file->bytes.data() + file->position, static_cast<std::size_t>(bytes));
    file->position += static_cast<std::size_t>(bytes);
    return 0;
}

int buffer_write(void* param, const void* data, uint64_t bytes)
{
    auto* file = static_cast<MemoryFile*>(param);
    if (!file || bytes > static_cast<uint64_t>(SIZE_MAX - file->position)) {
        return -1;
    }
    const auto end = file->position + static_cast<std::size_t>(bytes);
    if (end > file->max_bytes) {
        return -1;
    }
    if (end > file->bytes.size()) {
        file->bytes.resize(end);
    }
    std::memcpy(file->bytes.data() + file->position, data, static_cast<std::size_t>(bytes));
    file->position = end;
    return 0;
}

int buffer_seek(void* param, int64_t offset)
{
    auto* file = static_cast<MemoryFile*>(param);
    if (!file) {
        return -1;
    }
    const int64_t target = offset >= 0 ? offset : static_cast<int64_t>(file->bytes.size()) + offset;
    if (target < 0 || static_cast<uint64_t>(target) > file->max_bytes ||
        static_cast<uint64_t>(target) > std::numeric_limits<std::size_t>::max()) {
        return -1;
    }
    file->position = static_cast<std::size_t>(target);
    return 0;
}

int64_t buffer_tell(void* param)
{
    const auto* file = static_cast<MemoryFile*>(param);
    if (!file || file->position > static_cast<std::size_t>(std::numeric_limits<int64_t>::max())) {
        return -1;
    }
    return static_cast<int64_t>(file->position);
}

const mov_buffer_t kMemoryBuffer{
    buffer_read,
    buffer_write,
    buffer_seek,
    buffer_tell,
};

uint8_t video_object(media::Codec codec)
{
    return codec == media::Codec::H265 ? MOV_OBJECT_H265 : MOV_OBJECT_H264;
}

} // namespace

struct Mp4Muxer::Context {
    explicit Context(std::size_t max_bytes) : file(max_bytes)
    {
        writer = max_bytes > 0 ? mov_writer_create(&kMemoryBuffer, &file, MOV_FLAG_FASTSTART) : nullptr;
    }

    ~Context()
    {
        if (writer) {
            mov_writer_destroy(writer);
        }
    }

    MemoryFile file;
    mov_writer_t* writer{nullptr};
    int video_track{-1};
    int audio_track{-1};
    media::Codec video_codec{media::Codec::Unknown};
    media::Codec audio_codec{media::Codec::Unknown};
    mpeg4_avc_t avc{};
    mpeg4_hevc_t hevc{};
    std::vector<uint8_t> sample_buffer;
};

Mp4Muxer::Mp4Muxer(std::size_t max_bytes) : context_(std::make_unique<Context>(max_bytes))
{
}

Mp4Muxer::~Mp4Muxer() = default;

bool Mp4Muxer::configure_video(const media::VideoFormat& format)
{
    if (!context_ || !context_->writer || context_->video_track >= 0 ||
        format.width <= 0 || format.height <= 0 || format.extradata.empty()) {
        return false;
    }
    if (format.codec == media::Codec::H264) {
        if (mpeg4_avc_decoder_configuration_record_load(format.extradata.data(), format.extradata.size(), &context_->avc) <= 0) {
            return false;
        }
    } else if (format.codec == media::Codec::H265) {
        if (mpeg4_hevc_decoder_configuration_record_load(format.extradata.data(), format.extradata.size(), &context_->hevc) <= 0) {
            return false;
        }
    } else {
        return false;
    }
    const int track = mov_writer_add_video(
        context_->writer, video_object(format.codec), format.width, format.height,
        format.extradata.data(), format.extradata.size());
    if (track < 0) {
        return false;
    }
    context_->video_track = track;
    context_->video_codec = format.codec;
    return true;
}

bool Mp4Muxer::configure_audio(const media::AudioFormat& format)
{
    if (!context_ || !context_->writer || context_->audio_track >= 0 ||
        format.sample_rate <= 0 || format.channels <= 0 ||
        (format.codec != media::Codec::AAC && format.codec != media::Codec::G711A &&
         format.codec != media::Codec::G711U)) {
        return false;
    }
    if (format.codec == media::Codec::AAC) {
        mpeg4_aac_t aac{};
        if (format.extradata.empty() ||
            mpeg4_aac_audio_specific_config_load(format.extradata.data(), format.extradata.size(), &aac) < 0) {
            return false;
        }
    }
    const uint8_t object = format.codec == media::Codec::G711U ? MOV_OBJECT_G711u :
                           format.codec == media::Codec::G711A ? MOV_OBJECT_G711a : MOV_OBJECT_AAC;
    const int track = mov_writer_add_audio(
        context_->writer, object, format.channels,
        format.bits_per_sample > 0 ? format.bits_per_sample : 16,
        format.sample_rate, format.extradata.data(), format.extradata.size());
    if (track < 0) {
        return false;
    }
    context_->audio_track = track;
    context_->audio_codec = format.codec;
    return true;
}

bool Mp4Muxer::write_frame(const std::shared_ptr<const media::MediaFrame>& frame)
{
    if (!frame || !context_ || !context_->writer || frame->size() == 0) {
        return false;
    }
    const bool video = frame->type() == media::MediaType::Video;
    const int track = video ? context_->video_track : context_->audio_track;
    const media::Codec codec = video ? context_->video_codec : context_->audio_codec;
    if (track < 0 || frame->codec() != codec || frame->pts_ms() < 0 || frame->dts_ms() < 0 ||
        frame->codec_config()) {
        return false;
    }
    const int flags = frame->keyframe() ? MOV_AV_FLAG_KEYFREAME : 0;
    if (!video) {
        if (codec == media::Codec::AAC) {
            mpeg4_aac_t aac{};
            if (frame->size() >= 7 && mpeg4_aac_adts_load(frame->data(), frame->size(), &aac) >= 7) {
                return false;
            }
        }
        return mov_writer_write(context_->writer, track, frame->data(), frame->size(),
                                frame->pts_ms(), frame->dts_ms(), flags) == 0;
    }

    context_->sample_buffer.resize(frame->size() + frame->nalus().size() * 4 + 64);
    int vcl = 0;
    int update = 0;
    int bytes = context_->video_codec == media::Codec::H264
        ? h264_annexbtomp4(&context_->avc, frame->data(), frame->size(), context_->sample_buffer.data(), context_->sample_buffer.size(), &vcl, &update)
        : h265_annexbtomp4(&context_->hevc, frame->data(), frame->size(), context_->sample_buffer.data(), context_->sample_buffer.size(), &vcl, &update);
    if (bytes <= 0) {
        return false;
    }
    return mov_writer_write(context_->writer, track, context_->sample_buffer.data(), static_cast<std::size_t>(bytes),
                            frame->pts_ms(), frame->dts_ms(), flags) == 0;
}

bool Mp4Muxer::finalize(MuxedDataSink output)
{
    if (!context_ || !context_->writer || !output) {
        return false;
    }
    mov_writer_destroy(context_->writer);
    context_->writer = nullptr;
    return output(context_->file.bytes.data(), context_->file.bytes.size());
}

} // namespace zero_ipc::packaging
