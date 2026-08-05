#pragma once

#include "resource_profile.h"
#include "device_pipeline.h"
#include "poller_runtime.h"
#include "protocol_plugin.h"
#include "media/detection_hub.h"
#include "media/media_source.h"
#include <cstddef>
#include <memory>
#include <vector>

namespace zero_ipc::core {

struct ApplicationOptions {
    ResourceProfile profile{ResourceProfile::hisi_32mb()};
    int channel_id{0};
};

/**
 * Composition root (CRP + LoD).
 * - Owns MediaSource and the shared protocol poller
 * - Depends on IDevicePipeline / IProtocolPlugin abstractions (DIP)
 * - New protocols: add_protocol() only �?Application internals stay closed (OCP)
 */
class Application final {
public:
    Application(ApplicationOptions options,
                std::unique_ptr<IDevicePipeline> device);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void add_protocol(std::unique_ptr<IProtocolPlugin> protocol);

    bool start();
    void stop();

    const ResourceProfile& profile() const noexcept { return options_.profile; }

private:
    ApplicationOptions options_;
    std::shared_ptr<media::MediaSource> media_;
    PollerRuntime poller_runtime_;
    std::unique_ptr<IDevicePipeline> device_;
    std::vector<std::unique_ptr<IProtocolPlugin>> protocols_;
    std::size_t started_protocol_count_{0};
    bool poller_started_{false};
    bool device_started_{false};
    bool running_{false};
};

} // namespace zero_ipc::core
