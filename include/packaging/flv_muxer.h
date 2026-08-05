#pragma once

#include "flv_tag_sink.h"
#include "media/media_frame.h"
#include <memory>

namespace zero_ipc::packaging {

class FlvMuxer final {
public:
    explicit FlvMuxer(FlvTagSink output);
    ~FlvMuxer();

    FlvMuxer(const FlvMuxer&) = delete;
    FlvMuxer& operator=(const FlvMuxer&) = delete;

    bool configure_video(const media::VideoFormat& format);
    bool configure_audio(const media::AudioFormat& format);
    bool write_frame(const std::shared_ptr<const media::MediaFrame>& frame);
    bool write_frame(const std::shared_ptr<const media::MediaFrame>& frame,
                     int64_t timestamp_base_ms);
    void reset();

private:
    struct Context;
    static int on_packet(void* param, int type, const void* data,
                         std::size_t bytes, uint32_t timestamp);

    std::unique_ptr<Context> context_;
};

} // namespace zero_ipc::packaging
