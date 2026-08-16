#pragma once

#include "media/media_format.h"
#include "media/media_frame.h"
#include <memory>
#include <optional>
#include <string>

namespace ipc_mini::record {

/**
 * One recording session: shared init.mp4 + sequential media segments.
 * Same writer across files so timestamps stay continuous for gapless play.
 */
class ICmafMuxer {
public:
    virtual ~ICmafMuxer() = default;

    virtual bool start_session(const media::VideoFormat& video,
                               const std::optional<media::AudioFormat>& audio,
                               const std::string& init_path) = 0;
    virtual bool open_segment(const std::string& media_path) = 0;
    virtual bool write_frame(
        const std::shared_ptr<const media::MediaFrame>& frame) = 0;
    virtual bool close_segment() = 0;
    virtual void end_session() = 0;
    virtual bool segment_open() const noexcept = 0;
};

} // namespace ipc_mini::record
