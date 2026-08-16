#pragma once

#include "encoded_frame.h"
#include "config/stream_config.h"
#include "media/media_source.h"
#include <memory>
#include <string>

/*
 * stream_observer.h includes <thread>, and also declares on_stream_error(..., int32_t errno).
 * Pre-include <thread> while errno macro is intact, then undef only for stream_observer parse.
 */
#include <cerrno>
#include <mutex>
#include <thread>

#pragma push_macro("errno")
#undef errno
#include <stream_observer.h>
#pragma pop_macro("errno")

#include <dev_chn.h>

namespace zero_ipc::device_adapter {

class EncodedStreamChannel final : public hisilicon::dev::chn {
public:
    EncodedStreamChannel(const char* sensor_name, const char* encoder_mode,
                         int32_t channel_id,
                         std::shared_ptr<media::MediaSource> media_source);

    void on_stream_come(zero_ipc::util::stream_obj_ptr sobj,
                        zero_ipc::util::stream_head* head,
                        const char* buf, int32_t len) override;
    void on_stream_error(zero_ipc::util::stream_obj_ptr sobj, int32_t error_code) override;

    bool register_encoded_tracks(const config::StreamsConfig& streams,
                                 media::Codec video_codec);
    bool start_encoders(const config::EncodedStreamConfig& main,
                        const config::EncodedStreamConfig& sub);
    bool register_audio_tracks(media::Codec audio_codec, int sample_rate,
                               int channels, int bits_per_sample);

private:
    std::shared_ptr<media::MediaSource> media_source_;
    std::string encoder_mode_;
    media::Codec audio_codec_{media::Codec::Unknown};
};

} // namespace zero_ipc::device_adapter
