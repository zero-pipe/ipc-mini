#include "media/gop_cache.h"

namespace zero_ipc::media {

GopCache::GopCache(GopCacheConfig config) : config_(config)
{
}

void GopCache::start_new_gop_locked(std::shared_ptr<const MediaFrame> frame)
{
    CachedGop gop;
    gop.start_pts_ms = frame->pts_ms();
    gop.end_pts_ms = frame->pts_ms();
    gop.bytes = frame->size();
    gop.frames.push_back(std::move(frame));
    bytes_ += gop.bytes;
    gops_.push_back(std::move(gop));
}

bool GopCache::push(std::shared_ptr<const MediaFrame> frame)
{
    if (!frame || frame->type() != MediaType::Video || frame->size() == 0) {
        return false;
    }
    std::lock_guard lock(mutex_);
    if (config_.max_gops == 0 || config_.max_bytes == 0 ||
        frame->size() > config_.max_bytes) {
        return false;
    }
    if (frame->keyframe()) {
        start_new_gop_locked(std::move(frame));
        trim_locked();
        return true;
    }
    if (gops_.empty()) {
        return false;
    }

    auto& gop = gops_.back();
    if (gop.bytes > config_.max_bytes - frame->size() ||
        (config_.max_duration_ms > 0 &&
         frame->pts_ms() - gop.start_pts_ms > config_.max_duration_ms)) {
        return false;
    }
    gop.end_pts_ms = frame->pts_ms();
    gop.bytes += frame->size();
    bytes_ += frame->size();
    gop.frames.push_back(std::move(frame));
    trim_locked();
    return true;
}

void GopCache::trim_locked()
{
    while (gops_.size() > config_.max_gops || bytes_ > config_.max_bytes ||
           (gops_.size() > 1 && config_.max_duration_ms > 0 &&
            gops_.back().end_pts_ms - gops_.front().start_pts_ms > config_.max_duration_ms)) {
        bytes_ -= gops_.front().bytes;
        gops_.pop_front();
    }
}

std::vector<std::shared_ptr<const MediaFrame>> GopCache::snapshot_from_latest_key() const
{
    std::lock_guard lock(mutex_);
    if (gops_.empty()) {
        return {};
    }
    return gops_.back().frames;
}

} // namespace zero_ipc::media
