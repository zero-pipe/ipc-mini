#pragma once

#include "muxed_data_sink.h"
#include "media/media_frame.h"
#include <cstddef>
#include <memory>

namespace zero_ipc::packaging {

class Mp4Muxer final {
public:
    explicit Mp4Muxer(std::size_t max_bytes = 64 * 1024 * 1024);
    ~Mp4Muxer();

    Mp4Muxer(const Mp4Muxer&) = delete;
    Mp4Muxer& operator=(const Mp4Muxer&) = delete;

    bool configure_video(const media::VideoFormat& format);
    bool configure_audio(const media::AudioFormat& format);
    bool write_frame(const std::shared_ptr<const media::MediaFrame>& frame);
    bool finalize(MuxedDataSink output);

private:
    struct Context;
    std::unique_ptr<Context> context_;
};

} // namespace zero_ipc::packaging
