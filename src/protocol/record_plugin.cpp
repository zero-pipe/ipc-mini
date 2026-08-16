#include "protocol/record_plugin.h"

#include "record/duration_segment_policy.h"
#include "record/fmp4_segment_muxer.h"
#include "record/http_segment_uploader.h"
#include "record/segment_recorder.h"

#include <cstdio>
#include <utility>

namespace ipc_mini::protocol {

RecordPlugin::RecordPlugin(record::RecordConfig config)
    : config_(std::move(config))
{
}

RecordPlugin::~RecordPlugin()
{
    stop();
}

bool RecordPlugin::start(const core::ProtocolContext& context)
{
    if (recorder_ || !context.media_source || !config_.enabled) {
        return config_.enabled ? false : true;
    }
    if (config_.stream_id != 0 && config_.stream_id != 1) {
        return false;
    }
    if (config_.segment_sec <= 0) {
        config_.segment_sec = 300;
    }
    if (config_.directory.empty()) {
        config_.directory = "/mnt/record";
    }

    auto muxer = std::make_unique<record::Fmp4SegmentMuxer>();
    auto policy =
        std::make_unique<record::DurationSegmentPolicy>(config_.segment_sec);
    std::unique_ptr<record::ISegmentUploader> uploader;
    if (!config_.upload_url.empty()) {
        uploader = std::make_unique<record::HttpSegmentUploader>(
            config_.upload_url, config_.upload_token);
    } else {
        uploader = std::make_unique<record::NullSegmentUploader>();
    }

    recorder_ = std::make_unique<record::SegmentRecorder>(
        config_, context.media_source, std::move(muxer), std::move(policy),
        std::move(uploader));
    if (!recorder_->start()) {
        std::fprintf(stderr,
                     "[record] start failed (check %s); continue without "
                     "recording\n",
                     config_.directory.c_str());
        recorder_.reset();
        return true;
    }
    return true;
}

void RecordPlugin::stop()
{
    if (!recorder_) {
        return;
    }
    recorder_->stop();
    recorder_.reset();
}

} // namespace ipc_mini::protocol
