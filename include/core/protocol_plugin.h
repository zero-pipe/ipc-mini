#pragma once

#include "media/detection_hub.h"
#include "media/media_source.h"
#include <cstddef>
#include <memory>

struct ztk_poller_pool;

namespace zero_ipc::core {

/**
 * Shared runtime injected into every protocol plugin (DIP / LoD).
 * Plugins talk to MediaSource + DetectionHub + poller_pool only � never HiSilicon SDK.
 */
struct ProtocolContext {
    std::shared_ptr<media::MediaSource> media_source;
    std::shared_ptr<media::DetectionHub> detections;
    ztk_poller_pool* poller_pool{nullptr};
    std::size_t output_high_water_bytes{64 * 1024};
    std::size_t pending_frame_bytes_per_session{512 * 1024};
};

/**
 * Protocol plugin lifecycle contract.
 * OCP: add RTMP/GB28181/ONVIF by implementing this interface �?no Application edit.
 */
class IProtocolPlugin {
public:
    virtual ~IProtocolPlugin() = default;

    virtual bool start(const ProtocolContext& context) = 0;
    virtual void stop() = 0;
};

} // namespace zero_ipc::core
