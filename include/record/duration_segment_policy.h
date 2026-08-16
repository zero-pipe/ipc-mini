#pragma once

#include "i_segment_policy.h"

namespace ipc_mini::record {

/** Rotate on the next video keyframe after segment_sec elapsed (pts-based). */
class DurationSegmentPolicy final : public ISegmentPolicy {
public:
    explicit DurationSegmentPolicy(int segment_sec);

    void on_segment_started(int64_t pts_ms) override;
    bool should_rotate(
        const std::shared_ptr<const media::MediaFrame>& keyframe) const override;

private:
    int64_t segment_ms_{300000};
    int64_t segment_start_pts_ms_{-1};
};

} // namespace ipc_mini::record
