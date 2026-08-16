#pragma once

#include "media/gop_cache.h"
#include <cstddef>

namespace ipc_mini::core {

/**
 * Process-wide memory and worker limits.
 * Defaults target Hi3516-class boards (~32MB RAM / ~80MB flash).
 */
struct ResourceProfile {
    media::GopCacheConfig gop_cache{};
    int poller_count{1};
    std::size_t protocol_output_high_water_bytes{64 * 1024};
    std::size_t protocol_pending_frame_bytes_per_session{512 * 1024};

    static ResourceProfile hisi_32mb()
    {
        ResourceProfile profile;
        profile.gop_cache.max_gops = 1;
        profile.gop_cache.max_bytes = 512 * 1024;
        profile.gop_cache.max_duration_ms = 2000;
        profile.poller_count = 1;
        profile.protocol_output_high_water_bytes = 64 * 1024;
        /* KVS already owns a rolling buffer; keep only a small app-side queue. */
        profile.protocol_pending_frame_bytes_per_session = 128 * 1024;
        return profile;
    }

    static ResourceProfile host_development()
    {
        ResourceProfile profile;
        profile.gop_cache.max_gops = 2;
        profile.gop_cache.max_bytes = 4 * 1024 * 1024;
        profile.gop_cache.max_duration_ms = 5000;
        profile.poller_count = 1;
        profile.protocol_output_high_water_bytes = 128 * 1024;
        profile.protocol_pending_frame_bytes_per_session = 2 * 1024 * 1024;
        return profile;
    }
};

} // namespace ipc_mini::core
