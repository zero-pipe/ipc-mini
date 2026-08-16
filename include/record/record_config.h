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
    std::string directory{"/mnt/record"};
    /** http://host:port/record/door-1 — empty disables upload. */
    std::string upload_url;
    std::string upload_token;
    std::size_t max_pending_frames{120};
};

} // namespace ipc_mini::record
