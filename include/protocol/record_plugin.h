#pragma once

#include "core/protocol_plugin.h"
#include "record/record_config.h"
#include <memory>

namespace ipc_mini::record {
class SegmentRecorder;
}

namespace ipc_mini::protocol {

/**
 * Consumer fMP4 recorder as IProtocolPlugin.
 * Depends on MediaSource only — never HiSilicon.
 */
class RecordPlugin final : public core::IProtocolPlugin {
public:
    explicit RecordPlugin(record::RecordConfig config);
    ~RecordPlugin() override;

    bool start(const core::ProtocolContext& context) override;
    void stop() override;

private:
    record::RecordConfig config_;
    std::unique_ptr<record::SegmentRecorder> recorder_;
};

} // namespace ipc_mini::protocol
