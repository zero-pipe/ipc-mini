#pragma once

#include "media/detection.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace ipc_mini::media {

class DetectionSource final {
public:
    using Handler = std::function<void(const DetectionResult&)>;

    uint64_t subscribe(Handler handler);
    void unsubscribe(uint64_t id);
    void publish(DetectionResult result);
    std::size_t subscriber_count() const;

private:
    mutable std::mutex mutex_;
    uint64_t next_id_{1};
    std::unordered_map<uint64_t, Handler> handlers_;
};

} // namespace ipc_mini::media
