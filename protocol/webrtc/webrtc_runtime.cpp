#include "webrtc_plugin_impl.h"

#include <algorithm>
#include <cstdio>
#include <new>

namespace ipc_mini::protocol {
namespace webrtc_detail {

void run_sync_call(void* user)
{
    auto* call = static_cast<SyncCall*>(user);
    if (call && call->fn) {
        call->fn();
    }
    if (!call) {
        return;
    }
    {
        std::lock_guard lock(call->mutex);
        call->done = true;
    }
    call->cv.notify_one();
}

ztk_thread_pool* create_worker_pool()
{
    ztk_thread_pool_opts_t options{};
    options.thread_count = 1;
    options.priority = ZTK_THREAD_PRIO_NORMAL;
    options.auto_start = 1;
    return ztk_thread_pool_create(&options);
}

} // namespace webrtc_detail

void WebRtcPlugin::Impl::async_task(void* user)
{
    std::unique_ptr<AsyncTask> task(static_cast<AsyncTask*>(user));
    if (task && task->self && task->fn) {
        task->fn();
    }
}

void WebRtcPlugin::Impl::reap_task(void* user)
{
    std::unique_ptr<ReapTask> task(static_cast<ReapTask*>(user));
    if (task && task->peer) {
        task->peer->stop();
        task->peer.reset();
    }
}

bool WebRtcPlugin::Impl::post_signaling(std::function<void()> fn)
{
    if (!signaling_poller || !fn || !running.load()) {
        return false;
    }
    auto* task = new (std::nothrow) AsyncTask{this, std::move(fn)};
    if (!task) {
        return false;
    }
    if (ztk_poller_async(signaling_poller, &Impl::async_task, task, 0) !=
        ZTK_OK) {
        delete task;
        return false;
    }
    return true;
}

bool WebRtcPlugin::Impl::post_media(std::function<void()> fn)
{
    if (!media_pool || !fn || !running.load()) {
        return false;
    }
    auto* task = new (std::nothrow) AsyncTask{this, std::move(fn)};
    if (!task) {
        return false;
    }
    if (ztk_thread_pool_async(media_pool, &Impl::async_task, task, 0) !=
        ZTK_OK) {
        delete task;
        return false;
    }
    return true;
}

bool WebRtcPlugin::Impl::call_signaling_sync(std::function<void()> fn)
{
    if (!signaling_poller || !fn) {
        return false;
    }
    if (ztk_poller_is_current_thread(signaling_poller)) {
        fn();
        return true;
    }
    webrtc_detail::SyncCall call;
    call.fn = std::move(fn);
    if (ztk_poller_async(signaling_poller, &webrtc_detail::run_sync_call,
                         &call, 0) != ZTK_OK) {
        return false;
    }
    std::unique_lock lock(call.mutex);
    call.cv.wait(lock, [&call] { return call.done; });
    return true;
}

bool WebRtcPlugin::Impl::call_media_sync(std::function<void()> fn)
{
    if (!media_pool || !fn) {
        return false;
    }
    webrtc_detail::SyncCall call;
    call.fn = std::move(fn);
    if (ztk_thread_pool_async(media_pool, &webrtc_detail::run_sync_call,
                              &call, 0) != ZTK_OK) {
        return false;
    }
    std::unique_lock lock(call.mutex);
    call.cv.wait(lock, [&call] { return call.done; });
    return true;
}

void WebRtcPlugin::Impl::destroy_runtime()
{
    if (media_pool) {
        ztk_thread_pool_destroy(media_pool);
        media_pool = nullptr;
    }
    if (cleanup_pool) {
        ztk_thread_pool_destroy(cleanup_pool);
        cleanup_pool = nullptr;
    }
    if (signaling_pool) {
        ztk_poller_pool_stop(signaling_pool);
        ztk_poller_pool_destroy(signaling_pool);
        signaling_pool = nullptr;
    }
    signaling_poller = nullptr;
}

bool WebRtcPlugin::Impl::start_runtime()
{
    ztk_poller_pool_opts_t poller_options{};
    poller_options.size = 1;
    poller_options.prefer_current_thread = 0;
    poller_options.thread_priority = ZTK_THREAD_PRIO_NORMAL;
    signaling_pool = ztk_poller_pool_create(&poller_options);
    if (!signaling_pool ||
        ztk_poller_pool_start(signaling_pool) != ZTK_OK) {
        if (signaling_pool) {
            ztk_poller_pool_destroy(signaling_pool);
            signaling_pool = nullptr;
        }
        return false;
    }
    signaling_poller = ztk_poller_pool_get_first(signaling_pool);
    if (!signaling_poller) {
        destroy_runtime();
        return false;
    }

    media_pool = webrtc_detail::create_worker_pool();
    cleanup_pool = webrtc_detail::create_worker_pool();
    if (!media_pool || !cleanup_pool) {
        destroy_runtime();
        return false;
    }
    return true;
}

void WebRtcPlugin::Impl::queue_reap(
    std::shared_ptr<ipc_mini::webrtc_net::WebRtcPeerConnection> peer)
{
    if (!peer) {
        return;
    }
    peer->deactivate();
    auto* task = new (std::nothrow) ReapTask{std::move(peer)};
    if (!task) {
        return;
    }
    if (!cleanup_pool ||
        ztk_thread_pool_async(
            cleanup_pool, &Impl::reap_task, task, 0) != ZTK_OK) {
        std::fprintf(stderr, "[webrtc] cleanup queue full/failure\n");
        delete task;
    }
}

std::size_t WebRtcPlugin::Impl::viewer_limit() const
{
    return std::min<std::size_t>(
        webrtc_detail::kMaxViewerLimit,
        static_cast<std::size_t>(std::max(options.max_viewers, 1)));
}

ipc_mini::webrtc_net::PeerConnectionConfig
WebRtcPlugin::Impl::peer_config() const
{
    ipc_mini::webrtc_net::PeerConnectionConfig config;
    config.rolling_buffer_duration_sec = options.rolling_buffer_sec;
    config.expected_bitrate_bps = options.expected_bitrate_bps;
    config.disable_twcc = options.disable_twcc;
    config.enable_datachannel = options.detections_enabled;
    for (const auto& ice : options.ice_servers) {
        ipc_mini::webrtc_net::IceServerConfig server;
        server.urls = ice.urls;
        server.username = ice.username;
        server.credential = ice.credential;
        config.ice_servers.push_back(std::move(server));
    }
    return config;
}

} // namespace ipc_mini::protocol
