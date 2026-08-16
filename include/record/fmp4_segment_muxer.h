#pragma once

#include "i_cmaf_muxer.h"
#include <memory>

namespace ipc_mini::record {

/** CMAF fMP4 via zero-media-kit fmp4_writer (MOV_FLAG_SEGMENT). */
class Fmp4SegmentMuxer final : public ICmafMuxer {
public:
    Fmp4SegmentMuxer();
    ~Fmp4SegmentMuxer() override;

    Fmp4SegmentMuxer(const Fmp4SegmentMuxer&) = delete;
    Fmp4SegmentMuxer& operator=(const Fmp4SegmentMuxer&) = delete;

    bool start_session(const media::VideoFormat& video,
                       const std::optional<media::AudioFormat>& audio,
                       const std::string& init_path) override;
    bool open_segment(const std::string& media_path) override;
    bool write_frame(
        const std::shared_ptr<const media::MediaFrame>& frame) override;
    bool close_segment() override;
    void end_session() override;
    bool segment_open() const noexcept override;

private:
    struct Context;
    std::unique_ptr<Context> context_;

    bool attach_file(const std::string& path);
    void detach_file();
};

} // namespace ipc_mini::record
