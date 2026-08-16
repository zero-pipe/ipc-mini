#include "webrtc_plugin_impl.h"

#include <algorithm>
#include <cstdio>

namespace ipc_mini::protocol {

WebRtcPlugin::WebRtcPlugin(WebRtcPluginOptions options)
    : impl_(std::make_unique<Impl>())
{
    impl_->options = std::move(options);
}

WebRtcPlugin::~WebRtcPlugin()
{
    stop();
}

bool WebRtcPlugin::start(const core::ProtocolContext& context)
{
    if (impl_->running.load() || !context.media_source) {
        return false;
    }
    if (!impl_->start_runtime()) {
        return false;
    }

    impl_->media = context.media_source;
    impl_->detection_source = context.detection_source;
    if (context.pending_frame_bytes_per_session > 0) {
        impl_->video_queue_limit = std::min(
            webrtc_detail::kVideoQueueBytes,
            context.pending_frame_bytes_per_session);
    }
    impl_->running = true;

    ipc_mini::webrtc_net::SignalingConfig config;
    config.url = impl_->options.signaling_url;
    config.token = impl_->options.signaling_token;
    config.room = impl_->options.room;
    config.role = "master";
    auto signaling = std::make_shared<ipc_mini::webrtc_net::SignalingClient>(
        config, impl_->signaling_poller);
    signaling->set_handler(
        [this](const std::string& type, const std::string& json) {
            impl_->on_signaling_message(type, json);
        });
    {
        std::lock_guard lock(impl_->mutex);
        impl_->signaling = signaling;
    }
    if (!signaling->start()) {
        impl_->running = false;
        {
            std::lock_guard lock(impl_->mutex);
            impl_->signaling.reset();
            impl_->media.reset();
            impl_->detection_source.reset();
        }
        impl_->destroy_runtime();
        return false;
    }

    {
        std::lock_guard lock(impl_->mutex);
        impl_->pending_candidates.clear();
    }
    std::fprintf(stderr,
                 "[webrtc] plugin started room=%s max_viewers=%zu "
                 "signaling=owned media=pool cleanup=pool kvs=%s\n",
                 impl_->options.room.c_str(), impl_->viewer_limit(),
                 ipc_mini::webrtc_net::kvs_runtime_available()
                     ? "on" : "stub");
    return true;
}

void WebRtcPlugin::stop()
{
    if (!impl_->running.exchange(false)) {
        return;
    }

    std::shared_ptr<ipc_mini::webrtc_net::SignalingClient> signaling;
    {
        std::lock_guard lock(impl_->mutex);
        signaling = impl_->signaling;
    }
    if (signaling) {
        signaling->set_handler(nullptr);
        signaling->stop();
    }

    (void)impl_->call_media_sync([] {});
    (void)impl_->call_signaling_sync(
        [this] { impl_->remove_all_viewers("plugin-stop"); });
    (void)impl_->call_signaling_sync([] {});

    {
        std::lock_guard lock(impl_->mutex);
        impl_->signaling.reset();
        impl_->media.reset();
        impl_->detection_source.reset();
        impl_->pending_candidates.clear();
    }
    impl_->destroy_runtime();
    std::fprintf(stderr, "[webrtc] stopped\n");
}

} // namespace ipc_mini::protocol
