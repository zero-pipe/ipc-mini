#include "media/detection_source.h"

#include <vector>

namespace ipc_mini::media {

uint64_t DetectionSource::subscribe(Handler handler)
{
    if (!handler) {
        return 0;
    }
    std::lock_guard lock(mutex_);
    const uint64_t id = next_id_++;
    handlers_.emplace(id, std::move(handler));
    return id;
}

void DetectionSource::unsubscribe(uint64_t id)
{
    std::lock_guard lock(mutex_);
    handlers_.erase(id);
}

void DetectionSource::publish(DetectionResult result)
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
        handler(result);
    }
}

std::size_t DetectionSource::subscriber_count() const
{
    std::lock_guard lock(mutex_);
    return handlers_.size();
}

} // namespace ipc_mini::media
