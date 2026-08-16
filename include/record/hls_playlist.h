#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ipc_mini::record {

/** HLS EVENT playlist: init map + append-only segments for live/gapless play. */
class HlsPlaylist final {
public:
    bool begin(const std::string& path, int target_duration_sec);
    void append(const std::string& filename, double duration_sec);
    void finish();
    const std::string& path() const noexcept { return path_; }
    bool active() const noexcept { return !path_.empty(); }

private:
    bool rewrite() const;

    std::string path_;
    int target_duration_sec_{300};
    std::vector<std::pair<std::string, double>> entries_;
    bool ended_{false};
};

} // namespace ipc_mini::record
