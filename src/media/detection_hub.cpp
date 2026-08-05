#include "media/detection_hub.h"

namespace zero_ipc::media {

uint64_t DetectionHub::subscribe(Handler handler)
{
    if (!handler) {
        return 0;
    }
    std::lock_guard lock(mutex_);
    const uint64_t id = next_id_++;
    handlers_.emplace(id, std::move(handler));
    return id;
}

void DetectionHub::unsubscribe(uint64_t id)
{
    std::lock_guard lock(mutex_);
    handlers_.erase(id);
}

void DetectionHub::publish(DetectionFrame frame)
{
    std::vector<Handler> snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot.reserve(handlers_.size());
        for (auto& entry : handlers_) {
            snapshot.push_back(entry.second);
        }
    }
    for (auto& handler : snapshot) {
        handler(frame);
    }
}

std::size_t DetectionHub::subscriber_count() const
{
    std::lock_guard lock(mutex_);
    return handlers_.size();
}

} // namespace zero_ipc::media
