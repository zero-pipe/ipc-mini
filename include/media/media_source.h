#pragma once

#include "gop_cache.h"
#include "stream_track.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace zero_ipc::media {

using KeyframeRequestHandler =
    std::function<bool(int channel_id, int stream_id)>;
using StreamDemandHandler =
    std::function<bool(int channel_id, int stream_id, bool active)>;
using AudioPlaybackHandler =
    std::function<bool(const uint8_t* data, size_t len)>;
using FrameSubscriber =
    std::function<void(std::shared_ptr<const MediaFrame>)>;

class MediaSource final {
public:
    explicit MediaSource(int channel_id = 0, GopCacheConfig config = {});

    void set_keyframe_request_handler(KeyframeRequestHandler handler);
    void set_stream_demand_handler(StreamDemandHandler handler);
    void set_audio_playback_handler(AudioPlaybackHandler handler);
    void set_track(StreamTrack track);
    std::optional<StreamTrack> track(int stream_id, MediaType type) const;
    std::vector<std::shared_ptr<const MediaFrame>> gop_snapshot(int stream_id) const;
    bool publish(std::shared_ptr<const MediaFrame> frame);
    uint64_t subscribe(int stream_id, FrameSubscriber subscriber);
    void unsubscribe(int stream_id, uint64_t subscription_id);
    bool request_keyframe(int stream_id) const;
    bool play_g711u(const uint8_t* data, size_t len) const;

private:
    struct StreamState {
        std::optional<StreamTrack> video_track;
        std::optional<StreamTrack> audio_track;
        std::shared_ptr<GopCache> cache;
        std::unordered_map<uint64_t, std::shared_ptr<FrameSubscriber>> subscribers;
        std::shared_ptr<const std::vector<std::shared_ptr<FrameSubscriber>>> subscriber_snapshot;
    };

    mutable std::mutex mutex_;
    std::mutex demand_mutex_;
    uint64_t next_subscription_id_{1};
    std::unordered_map<int, StreamState> streams_;
    KeyframeRequestHandler keyframe_request_handler_;
    StreamDemandHandler stream_demand_handler_;
    AudioPlaybackHandler audio_playback_handler_;
    int channel_id_{0};
    GopCacheConfig cache_config_;
};

} // namespace zero_ipc::media
