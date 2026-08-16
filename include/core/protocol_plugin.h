#pragma once

#include "media/detection_hub.h"
#include "media/media_source.h"
#include <cstddef>
#include <memory>

namespace zero_ipc::core {

/**
 * Shared runtime injected into every protocol plugin (DIP / LoD).
 * Plugins talk to MediaSource + DetectionHub only — never HiSilicon SDK.
 * Each plugin owns its own I/O poller and worker threads.
 */
struct ProtocolContext {
    std::shared_ptr<media::MediaSource> media_source;
    std::shared_ptr<media::DetectionHub> detections;
    std::size_t output_high_water_bytes{64 * 1024};
    std::size_t pending_frame_bytes_per_session{512 * 1024};
};

/**
 * Protocol plugin lifecycle contract.
 * OCP: add RTMP/GB28181/ONVIF by implementing this interface — no Application edit.
 */
class IProtocolPlugin {
public:
    virtual ~IProtocolPlugin() = default;

    virtual bool start(const ProtocolContext& context) = 0;
    virtual void stop() = 0;
};

} // namespace zero_ipc::core
