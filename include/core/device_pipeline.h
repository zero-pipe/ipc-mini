#pragma once

#include "media/detection_hub.h"
#include "media/media_source.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace zero_ipc::core {

/**
 * Device / capture boundary (DIP).
 * Application depends on this abstraction; HiSilicon lives behind it.
 */
class IDevicePipeline {
public:
    virtual ~IDevicePipeline() = default;

    virtual bool start(const std::shared_ptr<media::MediaSource>& source) = 0;
    virtual void stop() = 0;
    virtual bool request_keyframe(int channel_id, int stream_id) = 0;
    virtual bool set_stream_active(int channel_id, int stream_id,
                                   bool active) = 0;
    /** Play raw G711U/µ-law payload on the local speaker (optional). */
    virtual bool play_g711u(const uint8_t* /*data*/, size_t /*len*/)
    {
        return false;
    }
    virtual void set_detection_hub(
        std::shared_ptr<media::DetectionHub> /*hub*/)
    {
    }
};

} // namespace zero_ipc::core
