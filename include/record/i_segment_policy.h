#pragma once

#include "media/media_frame.h"
#include <cstdint>
#include <memory>

namespace ipc_mini::record {

class ISegmentPolicy {
public:
    virtual ~ISegmentPolicy() = default;

    virtual void on_segment_started(int64_t pts_ms) = 0;

    /** Called only for video keyframes that may start a new file. */
    virtual bool should_rotate(
        const std::shared_ptr<const media::MediaFrame>& keyframe) const = 0;
};

} // namespace ipc_mini::record
