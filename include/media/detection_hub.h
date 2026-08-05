#pragma once

#include "media/detection.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace zero_ipc::media {

class DetectionHub final {
public:
    using Handler = std::function<void(const DetectionFrame&)>;

    uint64_t subscribe(Handler handler);
    void unsubscribe(uint64_t id);
    void publish(DetectionFrame frame);
    std::size_t subscriber_count() const;

private:
    mutable std::mutex mutex_;
    uint64_t next_id_{1};
    std::unordered_map<uint64_t, Handler> handlers_;
};

} // namespace zero_ipc::media
