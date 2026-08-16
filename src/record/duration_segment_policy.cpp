#include "record/duration_segment_policy.h"

namespace ipc_mini::record {

DurationSegmentPolicy::DurationSegmentPolicy(int segment_sec)
    : segment_ms_(segment_sec > 0
                      ? static_cast<int64_t>(segment_sec) * 1000
                      : 300000)
{
}

void DurationSegmentPolicy::on_segment_started(int64_t pts_ms)
{
    segment_start_pts_ms_ = pts_ms;
}

bool DurationSegmentPolicy::should_rotate(
    const std::shared_ptr<const media::MediaFrame>& keyframe) const
{
    if (!keyframe || segment_start_pts_ms_ < 0) {
        return false;
    }
    return keyframe->pts_ms() - segment_start_pts_ms_ >= segment_ms_;
}

} // namespace ipc_mini::record
