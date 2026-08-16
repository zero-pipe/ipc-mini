#pragma once

#include <cstddef>
#include <string>

namespace ipc_mini::record {

/** Consumer-electronics fMP4 recording (CMAF + HLS, optional cloud PUT). */
struct RecordConfig {
    bool enabled{false};
    /** 0 = main, 1 = sub. */
    int stream_id{0};
    bool audio{true};
    int segment_sec{300};
    /** 0 = no time cap. Default 1 hour. */
    int retain_sec{3600};
    /** 0 = no size cap. Default 200 MB. */
    int max_bytes_mb{200};
    std::string directory{"/mnt/record"};
    /** http://host:port/record/door-1 — empty disables upload. */
    std::string upload_url;
    std::string upload_token;
    std::size_t max_pending_frames{120};
};

} // namespace ipc_mini::record
