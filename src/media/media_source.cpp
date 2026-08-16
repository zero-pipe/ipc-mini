#include "media/media_source.h"

namespace ipc_mini::media {

MediaSource::MediaSource(int channel_id, GopCacheConfig config)
    : channel_id_(channel_id), cache_config_(config)
{
}

void MediaSource::set_keyframe_request_handler(KeyframeRequestHandler handler)
{
    std::lock_guard lock(mutex_);
    keyframe_request_handler_ = std::move(handler);
}

void MediaSource::set_stream_demand_handler(StreamDemandHandler handler)
{
    std::lock_guard lock(mutex_);
    stream_demand_handler_ = std::move(handler);
}

void MediaSource::set_audio_playback_handler(AudioPlaybackHandler handler)
{
    std::lock_guard lock(mutex_);
    audio_playback_handler_ = std::move(handler);
}

void MediaSource::set_track(StreamTrack track)
{
    std::lock_guard lock(mutex_);
    auto it = streams_.find(track.stream_id);
    if (it == streams_.end()) {
        StreamState state;
        state.cache = std::make_shared<GopCache>(cache_config_);
        state.subscriber_snapshot =
            std::make_shared<const std::vector<std::shared_ptr<FrameSubscriber>>>();
        it = streams_.emplace(track.stream_id, std::move(state)).first;
    }
    std::optional<StreamTrack>& destination =
        track.type == MediaType::Video
            ? it->second.video_track
            : it->second.audio_track;
    destination = std::move(track);
}

std::optional<StreamTrack> MediaSource::track(
    int stream_id, MediaType type) const
{
    std::lock_guard lock(mutex_);
    const auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return std::nullopt;
    }
    return type == MediaType::Video
        ? it->second.video_track
        : it->second.audio_track;
}

std::vector<std::shared_ptr<const MediaFrame>>
MediaSource::gop_snapshot(int stream_id) const
{
    std::shared_ptr<GopCache> cache;
    {
        std::lock_guard lock(mutex_);
        const auto it = streams_.find(stream_id);
        if (it == streams_.end()) {
            return {};
        }
        cache = it->second.cache;
    }
    return cache ? cache->snapshot_from_latest_key()
                 : std::vector<std::shared_ptr<const MediaFrame>>{};
}

bool MediaSource::publish(std::shared_ptr<const MediaFrame> frame)
{
    if (!frame) {
        return false;
    }
    std::shared_ptr<GopCache> cache;
    std::shared_ptr<const std::vector<std::shared_ptr<FrameSubscriber>>> subscribers;
    {
        std::lock_guard lock(mutex_);
        const auto it = streams_.find(frame->stream_id());
        if (it == streams_.end()) {
            return false;
        }
        cache = it->second.cache;
        subscribers = it->second.subscriber_snapshot;
    }
    if (cache && frame->type() == MediaType::Video) {
        cache->push(frame);
    }
    if (subscribers) {
        for (const auto& subscriber : *subscribers) {
            if (subscriber && *subscriber) {
                (*subscriber)(frame);
            }
        }
    }
    return true;
}

uint64_t MediaSource::subscribe(int stream_id, FrameSubscriber subscriber)
{
    if (!subscriber) {
        return 0;
    }
    std::lock_guard demand_lock(demand_mutex_);
    StreamDemandHandler demand_handler;
    bool first_subscriber = false;
    {
        std::lock_guard lock(mutex_);
        const auto it = streams_.find(stream_id);
        if (it == streams_.end()) {
            return 0;
        }
        first_subscriber = it->second.subscribers.empty();
        demand_handler = stream_demand_handler_;
    }
    if (first_subscriber && demand_handler &&
        !demand_handler(channel_id_, stream_id, true)) {
        return 0;
    }

    std::lock_guard lock(mutex_);
    const auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return 0;
    }
    const uint64_t id = next_subscription_id_++;
    it->second.subscribers.emplace(
        id, std::make_shared<FrameSubscriber>(std::move(subscriber)));
    auto snapshot = std::make_shared<std::vector<std::shared_ptr<FrameSubscriber>>>();
    snapshot->reserve(it->second.subscribers.size());
    for (const auto& entry : it->second.subscribers) {
        snapshot->push_back(entry.second);
    }
    it->second.subscriber_snapshot = std::move(snapshot);
    return id;
}

void MediaSource::unsubscribe(int stream_id, uint64_t subscription_id)
{
    std::lock_guard demand_lock(demand_mutex_);
    StreamDemandHandler demand_handler;
    bool last_subscriber = false;
    {
        std::lock_guard lock(mutex_);
        const auto it = streams_.find(stream_id);
        if (it == streams_.end() ||
            it->second.subscribers.erase(subscription_id) == 0) {
            return;
        }
        auto snapshot = std::make_shared<std::vector<std::shared_ptr<FrameSubscriber>>>();
        snapshot->reserve(it->second.subscribers.size());
        for (const auto& entry : it->second.subscribers) {
            snapshot->push_back(entry.second);
        }
        it->second.subscriber_snapshot = std::move(snapshot);
        last_subscriber = it->second.subscribers.empty();
        demand_handler = stream_demand_handler_;
    }
    if (last_subscriber && demand_handler) {
        demand_handler(channel_id_, stream_id, false);
    }
}

bool MediaSource::request_keyframe(int stream_id) const
{
    KeyframeRequestHandler handler;
    {
        std::lock_guard lock(mutex_);
        handler = keyframe_request_handler_;
    }
    return handler && handler(channel_id_, stream_id);
}

bool MediaSource::play_g711u(const uint8_t* data, size_t len) const
{
    AudioPlaybackHandler handler;
    {
        std::lock_guard lock(mutex_);
        handler = audio_playback_handler_;
    }
    return handler && handler(data, len);
}

} // namespace ipc_mini::media
