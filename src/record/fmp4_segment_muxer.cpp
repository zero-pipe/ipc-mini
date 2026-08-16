#include "record/fmp4_segment_muxer.h"

#include <fmp4-writer.h>
#include <mov-format.h>
#include <mpeg4-aac.h>
#include <mpeg4-avc.h>
#include <mpeg4-hevc.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace ipc_mini::record {
namespace {

struct FileBuffer {
    FILE* file{nullptr};
};

int buffer_read(void* param, void* data, uint64_t bytes)
{
    auto* file = static_cast<FileBuffer*>(param);
    if (!file || !file->file) {
        return -1;
    }
    return std::fread(data, 1, static_cast<size_t>(bytes), file->file) == bytes
        ? 0
        : -1;
}

int buffer_write(void* param, const void* data, uint64_t bytes)
{
    auto* file = static_cast<FileBuffer*>(param);
    if (!file || !file->file) {
        return -1;
    }
    return std::fwrite(data, 1, static_cast<size_t>(bytes), file->file) == bytes
        ? 0
        : -1;
}

int buffer_seek(void* param, int64_t offset)
{
    auto* file = static_cast<FileBuffer*>(param);
    if (!file || !file->file) {
        return -1;
    }
    if (offset >= 0) {
        return std::fseek(file->file, static_cast<long>(offset), SEEK_SET) == 0
            ? 0
            : -1;
    }
    return std::fseek(file->file, static_cast<long>(offset), SEEK_END) == 0
        ? 0
        : -1;
}

int64_t buffer_tell(void* param)
{
    auto* file = static_cast<FileBuffer*>(param);
    if (!file || !file->file) {
        return -1;
    }
    return static_cast<int64_t>(std::ftell(file->file));
}

const mov_buffer_t kFileBuffer{
    buffer_read,
    buffer_write,
    buffer_seek,
    buffer_tell,
};

uint8_t video_object(media::Codec codec)
{
    return codec == media::Codec::H265 ? MOV_OBJECT_H265 : MOV_OBJECT_H264;
}

uint8_t audio_object(media::Codec codec)
{
    switch (codec) {
    case media::Codec::G711U:
        return MOV_OBJECT_G711u;
    case media::Codec::G711A:
        return MOV_OBJECT_G711a;
    case media::Codec::AAC:
        return MOV_OBJECT_AAC;
    default:
        return 0;
    }
}

} // namespace

struct Fmp4SegmentMuxer::Context {
    FileBuffer file{};
    fmp4_writer_t* writer{nullptr};
    int video_track{-1};
    int audio_track{-1};
    media::Codec video_codec{media::Codec::Unknown};
    media::Codec audio_codec{media::Codec::Unknown};
    mpeg4_avc_t avc{};
    mpeg4_hevc_t hevc{};
    std::vector<uint8_t> sample_buffer;
};

Fmp4SegmentMuxer::Fmp4SegmentMuxer() = default;

Fmp4SegmentMuxer::~Fmp4SegmentMuxer()
{
    end_session();
}

bool Fmp4SegmentMuxer::attach_file(const std::string& path)
{
    detach_file();
    if (!context_) {
        return false;
    }
    context_->file.file = std::fopen(path.c_str(), "wb+");
    return context_->file.file != nullptr;
}

void Fmp4SegmentMuxer::detach_file()
{
    if (!context_ || !context_->file.file) {
        return;
    }
    std::fflush(context_->file.file);
    std::fclose(context_->file.file);
    context_->file.file = nullptr;
}

bool Fmp4SegmentMuxer::start_session(const media::VideoFormat& video,
                                     const std::optional<media::AudioFormat>& audio,
                                     const std::string& init_path)
{
    end_session();
    if (init_path.empty() || video.width <= 0 || video.height <= 0 ||
        video.extradata.empty() ||
        (video.codec != media::Codec::H264 &&
         video.codec != media::Codec::H265)) {
        return false;
    }

    context_ = std::make_unique<Context>();
    context_->writer = fmp4_writer_create(
        &kFileBuffer, &context_->file, MOV_FLAG_SEGMENT);
    if (!context_->writer) {
        end_session();
        return false;
    }

    if (video.codec == media::Codec::H264) {
        if (mpeg4_avc_decoder_configuration_record_load(
                video.extradata.data(), video.extradata.size(),
                &context_->avc) <= 0) {
            end_session();
            return false;
        }
    } else if (mpeg4_hevc_decoder_configuration_record_load(
                   video.extradata.data(), video.extradata.size(),
                   &context_->hevc) <= 0) {
        end_session();
        return false;
    }

    context_->video_track = fmp4_writer_add_video(
        context_->writer, video_object(video.codec), video.width, video.height,
        video.extradata.data(), video.extradata.size());
    if (context_->video_track < 0) {
        end_session();
        return false;
    }
    context_->video_codec = video.codec;

    if (audio) {
        const auto& a = *audio;
        if (a.sample_rate <= 0 || a.channels <= 0 ||
            audio_object(a.codec) == 0) {
            end_session();
            return false;
        }
        if (a.codec == media::Codec::AAC && a.extradata.empty()) {
            end_session();
            return false;
        }
        context_->audio_track = fmp4_writer_add_audio(
            context_->writer, audio_object(a.codec), a.channels,
            a.bits_per_sample > 0 ? a.bits_per_sample : 16, a.sample_rate,
            a.extradata.data(), a.extradata.size());
        if (context_->audio_track < 0) {
            end_session();
            return false;
        }
        context_->audio_codec = a.codec;
    }

    if (!attach_file(init_path)) {
        end_session();
        return false;
    }
    if (fmp4_writer_init_segment(context_->writer) != 0) {
        end_session();
        return false;
    }
    detach_file();
    return true;
}

bool Fmp4SegmentMuxer::open_segment(const std::string& media_path)
{
    if (!context_ || !context_->writer || media_path.empty()) {
        return false;
    }
    if (context_->file.file) {
        close_segment();
    }
    return attach_file(media_path);
}

bool Fmp4SegmentMuxer::write_frame(
    const std::shared_ptr<const media::MediaFrame>& frame)
{
    if (!frame || !context_ || !context_->writer || !context_->file.file ||
        frame->size() == 0 || frame->codec_config() || frame->pts_ms() < 0 ||
        frame->dts_ms() < 0) {
        return false;
    }

    const bool video = frame->type() == media::MediaType::Video;
    const int track = video ? context_->video_track : context_->audio_track;
    const media::Codec codec =
        video ? context_->video_codec : context_->audio_codec;
    if (track < 0 || frame->codec() != codec) {
        return false;
    }

    const int flags = frame->keyframe() ? MOV_AV_FLAG_KEYFREAME : 0;
    if (!video) {
        const uint8_t* data = frame->data();
        std::size_t size = frame->size();
        if (codec == media::Codec::AAC && size >= 7) {
            mpeg4_aac_t aac{};
            const int header = mpeg4_aac_adts_load(data, size, &aac);
            if (header >= 7 && static_cast<std::size_t>(header) < size) {
                data += header;
                size -= static_cast<std::size_t>(header);
            }
        }
        return fmp4_writer_write(context_->writer, track, data, size,
                                 frame->pts_ms(), frame->dts_ms(), flags) == 0;
    }

    context_->sample_buffer.resize(
        frame->size() + frame->nalus().size() * 4 + 64);
    int vcl = 0;
    int update = 0;
    const int bytes = context_->video_codec == media::Codec::H264
        ? h264_annexbtomp4(&context_->avc, frame->data(), frame->size(),
                           context_->sample_buffer.data(),
                           context_->sample_buffer.size(), &vcl, &update)
        : h265_annexbtomp4(&context_->hevc, frame->data(), frame->size(),
                           context_->sample_buffer.data(),
                           context_->sample_buffer.size(), &vcl, &update);
    if (bytes <= 0) {
        return false;
    }
    return fmp4_writer_write(
               context_->writer, track, context_->sample_buffer.data(),
               static_cast<size_t>(bytes), frame->pts_ms(), frame->dts_ms(),
               flags) == 0;
}

bool Fmp4SegmentMuxer::close_segment()
{
    if (!context_ || !context_->writer || !context_->file.file) {
        return true;
    }
    fmp4_writer_save_segment(context_->writer);
    detach_file();
    return true;
}

void Fmp4SegmentMuxer::end_session()
{
    close_segment();
    if (!context_) {
        return;
    }
    if (context_->writer) {
        fmp4_writer_destroy(context_->writer);
        context_->writer = nullptr;
    }
    context_.reset();
}

bool Fmp4SegmentMuxer::segment_open() const noexcept
{
    return context_ && context_->writer && context_->file.file;
}

} // namespace ipc_mini::record
