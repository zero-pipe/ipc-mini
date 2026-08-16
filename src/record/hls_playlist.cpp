#include "record/hls_playlist.h"

#include <cmath>
#include <cstdio>

namespace ipc_mini::record {

bool HlsPlaylist::begin(const std::string& path, int target_duration_sec)
{
    path_ = path;
    target_duration_sec_ = target_duration_sec > 0 ? target_duration_sec : 300;
    media_sequence_ = 0;
    entries_.clear();
    ended_ = false;
    return rewrite();
}

void HlsPlaylist::append(const std::string& filename, double duration_sec)
{
    if (path_.empty() || filename.empty()) {
        return;
    }
    const double duration = duration_sec > 0.001 ? duration_sec : 0.001;
    const int rounded = static_cast<int>(std::ceil(duration));
    if (rounded > target_duration_sec_) {
        target_duration_sec_ = rounded;
    }
    entries_.emplace_back(filename, duration);
    ended_ = false;
    rewrite();
}

bool HlsPlaylist::drop_front_if(const std::string& filename)
{
    if (entries_.empty() || entries_.front().first != filename) {
        return false;
    }
    entries_.erase(entries_.begin());
    ++media_sequence_;
    rewrite();
    return true;
}

std::string HlsPlaylist::front_name() const
{
    return entries_.empty() ? std::string() : entries_.front().first;
}

void HlsPlaylist::finish()
{
    if (path_.empty() || ended_) {
        return;
    }
    ended_ = true;
    rewrite();
}

bool HlsPlaylist::rewrite() const
{
    if (path_.empty()) {
        return false;
    }
    FILE* file = std::fopen(path_.c_str(), "wb");
    if (!file) {
        std::fprintf(stderr, "[record] playlist open failed: %s\n",
                     path_.c_str());
        return false;
    }
    std::fprintf(file,
                 "#EXTM3U\n"
                 "#EXT-X-VERSION:7\n"
                 "#EXT-X-TARGETDURATION:%d\n"
                 "#EXT-X-MEDIA-SEQUENCE:%d\n"
                 "#EXT-X-INDEPENDENT-SEGMENTS\n"
                 "#EXT-X-MAP:URI=\"init.mp4\"\n",
                 target_duration_sec_, media_sequence_);
    for (const auto& entry : entries_) {
        std::fprintf(file, "#EXTINF:%.3f,\n%s\n", entry.second,
                     entry.first.c_str());
    }
    if (ended_) {
        std::fputs("#EXT-X-ENDLIST\n", file);
    }
    std::fclose(file);
    return true;
}

} // namespace ipc_mini::record
