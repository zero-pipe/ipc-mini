#include "core/application.h"
#include "media/detection_source.h"

#include <utility>

namespace ipc_mini::core {

Application::Application(ApplicationOptions options,
                         std::unique_ptr<IDevicePipeline> device)
    : options_(std::move(options)), device_(std::move(device))
{
}

Application::~Application()
{
    stop();
}

void Application::add_protocol(std::unique_ptr<IProtocolPlugin> protocol)
{
    if (protocol) {
        protocols_.push_back(std::move(protocol));
    }
}

bool Application::start()
{
    if (running_) {
        return true;
    }
    if (!device_) {
        return false;
    }

    media_ = std::make_shared<media::MediaSource>(options_.channel_id,
                                                  options_.profile.gop_cache);
    auto detection_source = std::make_shared<media::DetectionSource>();
    media_->set_keyframe_request_handler(
        [this](int channel_id, int stream_id) {
            return device_ && device_->request_keyframe(channel_id, stream_id);
        });
    media_->set_stream_demand_handler(
        [this](int channel_id, int stream_id, bool active) {
            return device_ &&
                device_->set_stream_active(channel_id, stream_id, active);
        });
    media_->set_audio_playback_handler(
        [this](const uint8_t* data, size_t len) {
            return device_ && device_->play_g711u(data, len);
        });
    device_->set_detection_source(detection_source);

    if (!device_->start(media_)) {
        media_.reset();
        return false;
    }
    device_started_ = true;

    if (!protocols_.empty()) {
        ProtocolContext context;
        context.media_source = media_;
        context.detection_source = detection_source;
        context.output_high_water_bytes =
            options_.profile.protocol_output_high_water_bytes;
        context.pending_frame_bytes_per_session =
            options_.profile.protocol_pending_frame_bytes_per_session;

        for (auto& protocol : protocols_) {
            if (!protocol->start(context)) {
                protocol->stop();
                for (std::size_t i = started_protocol_count_; i > 0; --i) {
                    protocols_[i - 1]->stop();
                }
                started_protocol_count_ = 0;
                if (device_started_) {
                    device_->stop();
                    device_started_ = false;
                }
                media_.reset();
                return false;
            }
            ++started_protocol_count_;
        }
    }

    running_ = true;
    return true;
}

void Application::stop()
{
    for (std::size_t i = started_protocol_count_; i > 0; --i) {
        protocols_[i - 1]->stop();
    }
    started_protocol_count_ = 0;
    if (device_started_ && device_) {
        device_->stop();
        device_started_ = false;
    }
    media_.reset();
    running_ = false;
}

} // namespace ipc_mini::core
