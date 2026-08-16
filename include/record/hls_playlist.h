#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ipc_mini::record {

/** Sliding-window HLS playlist (init map + media segments). */
class HlsPlaylist final {
public:
    bool begin(const std::string& path, int target_duration_sec);
    void append(const std::string& filename, double duration_sec);
    bool drop_front_if(const std::string& filename);
    void finish();
    const std::string& path() const noexcept { return path_; }
    bool active() const noexcept { return !path_.empty(); }
    std::string front_name() const;

private:
    bool rewrite() const;

    std::string path_;
    int target_duration_sec_{300};
    int media_sequence_{0};
    std::vector<std::pair<std::string, double>> entries_;
    bool ended_{false};
};

} // namespace ipc_mini::record
