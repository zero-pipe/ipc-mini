#pragma once

#include "resource_profile.h"
#include "device_pipeline.h"
#include "protocol_plugin.h"
#include "media/media_source.h"
#include <cstddef>
#include <memory>
#include <vector>

namespace ipc_mini::core {

struct ApplicationOptions {
    ResourceProfile profile{ResourceProfile::hisi_32mb()};
    int channel_id{0};
};

/**
 * Composition root (CRP + LoD).
 * - Owns MediaSource
 * - Depends on IDevicePipeline / IProtocolPlugin abstractions (DIP)
 * - New protocols: add_protocol() only — Application internals stay closed (OCP)
 * - Protocol I/O threads are owned by each plugin, not by Application
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
    std::unique_ptr<IDevicePipeline> device_;
    std::vector<std::unique_ptr<IProtocolPlugin>> protocols_;
    std::size_t started_protocol_count_{0};
    bool device_started_{false};
    bool running_{false};
};

} // namespace ipc_mini::core
