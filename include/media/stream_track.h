#pragma once

#include "media_format.h"
#include <cstdint>

namespace zero_ipc::media {

/**
 * Static registration metadata for one track on a stream_id.
 * Not used for per-frame transport — see MediaFrame for that.
 *
 * Exactly one of video / audio is meaningful; the active branch is selected by type.
 */
struct StreamTrack {
    int stream_id{0};
    MediaType type{MediaType::Video};
    /** RTP clock: 90000 for video, sample_rate for audio. */
    int clock_rate{90000};

    VideoFormat video;  // valid when type == MediaType::Video
    AudioFormat audio;  // valid when type == MediaType::Audio

    bool is_video() const { return type == MediaType::Video; }
    bool is_audio() const { return type == MediaType::Audio; }
    Codec codec() const
    {
        return is_video() ? video.codec : audio.codec;
    }
};

} // namespace zero_ipc::media
