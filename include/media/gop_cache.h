#pragma once

#include "media_frame.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace ipc_mini::media {

struct GopCacheConfig {
    std::size_t max_gops{4};
    std::size_t max_bytes{32 * 1024 * 1024};
    int64_t max_duration_ms{5000};
};

struct CachedGop {
    std::vector<std::shared_ptr<const MediaFrame>> frames;
    int64_t start_pts_ms{0};
    int64_t end_pts_ms{0};
    std::size_t bytes{0};
};

class GopCache final {
public:
    explicit GopCache(GopCacheConfig config = {});

    bool push(std::shared_ptr<const MediaFrame> frame);
    std::vector<std::shared_ptr<const MediaFrame>> snapshot_from_latest_key() const;

private:
    void trim_locked();
    void start_new_gop_locked(std::shared_ptr<const MediaFrame> frame);

    const GopCacheConfig config_;
    mutable std::mutex mutex_;
    std::deque<CachedGop> gops_;
    std::size_t bytes_{0};
};

} // namespace ipc_mini::media
