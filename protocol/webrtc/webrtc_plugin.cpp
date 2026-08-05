#include "protocol/webrtc_plugin.h"

#include "kvs_peer_session.h"
#include "signaling_client.h"
#include "signaling_message_codec.h"

#include <ztk/poller/poller.h>
#include <ztk/poller/poller_pool.h>
#include <ztk/thread/thread_pool.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace zero_ipc::protocol {
namespace {

using namespace zero_mini::webrtc_net;

constexpr std::size_t kMaxViewerLimit = 3;
constexpr std::size_t kVideoQueueFrames = 4;
constexpr std::size_t kVideoQueueBytes = 128 * 1024;
constexpr std::size_t kAudioQueueFrames = 16;
constexpr std::size_t kAudioQueueBytes = 16 * 1024;
constexpr std::size_t kPendingCandidateLimit = 32;

struct SyncCall {
    std::function<void()> fn;
    std::mutex mutex;
    std::condition_variable cv;
    bool done{false};
};

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

} // namespace

struct WebRtcPlugin::Impl {
    struct PendingCandidate {
        std::string candidate;
        std::string mid;
    };

    struct ViewerSession {
        std::string viewer_id;
        std::shared_ptr<KvsPeerSession> peer;
        bool remote_description_ready{false};
        std::deque<std::shared_ptr<const media::MediaFrame>> video_queue;
        std::deque<std::shared_ptr<const media::MediaFrame>> audio_queue;
        std::size_t video_bytes{0};
        std::size_t audio_bytes{0};
        std::optional<media::DetectionFrame> latest_detection;
        bool connected{false};
    };

    struct AsyncTask {
        Impl* self{nullptr};
        std::function<void()> fn;
    };

    struct ReapTask {
        std::shared_ptr<KvsPeerSession> peer;
    };

    WebRtcPluginOptions options;
    std::shared_ptr<media::MediaSource> media;
    std::shared_ptr<media::DetectionHub> detections;
    std::shared_ptr<SignalingClient> signaling;
    ztk_poller* poller{nullptr};
    ztk_thread_pool* cleanup_pool{nullptr};

    std::mutex mutex;
    std::map<std::string, std::shared_ptr<ViewerSession>> viewers;
    std::map<std::string, std::deque<PendingCandidate>> pending_candidates;
    uint64_t video_sub{0};
    uint64_t ai_hold_sub{0};
    uint64_t detect_sub{0};
    std::size_t video_queue_limit{kVideoQueueBytes};
    bool drain_scheduled{false};
    bool keyframe_pending{false};
    std::atomic<bool> running{false};

    static void async_task(void* user)
    {
        std::unique_ptr<AsyncTask> task(static_cast<AsyncTask*>(user));
        if (task && task->self && task->fn) {
            task->fn();
        }
    }

    bool post(std::function<void()> fn)
    {
        if (!poller || !fn || !running.load()) {
            return false;
        }
        auto* task = new (std::nothrow) AsyncTask{this, std::move(fn)};
        if (!task) {
            return false;
        }
        if (ztk_poller_async(poller, &Impl::async_task, task, 0) != ZTK_OK) {
            delete task;
            return false;
        }
        return true;
    }

    bool call_poller_sync(std::function<void()> fn)
    {
        if (!poller || !fn) {
            return false;
        }
        if (ztk_poller_is_current_thread(poller)) {
            fn();
            return true;
        }
        SyncCall call;
        call.fn = std::move(fn);
        if (ztk_poller_async(poller, &run_sync_call, &call, 0) != ZTK_OK) {
            return false;
        }
        std::unique_lock lock(call.mutex);
        call.cv.wait(lock, [&call] { return call.done; });
        return true;
    }

    static void reap_task(void* user)
    {
        std::unique_ptr<ReapTask> task(static_cast<ReapTask*>(user));
        if (task && task->peer) {
            task->peer->stop();
            task->peer.reset();
        }
    }

    void queue_reap(
        std::shared_ptr<KvsPeerSession> peer)
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

    std::size_t viewer_limit() const
    {
        return std::min<std::size_t>(
            kMaxViewerLimit,
            static_cast<std::size_t>(std::max(options.max_viewers, 1)));
    }

    PeerSessionConfig peer_config() const
    {
        PeerSessionConfig config;
        config.preview_stream_id = options.preview_stream_id;
        config.rolling_buffer_duration_sec = options.rolling_buffer_sec;
        config.expected_bitrate_bps = options.expected_bitrate_bps;
        config.disable_twcc = options.disable_twcc;
        config.enable_datachannel = options.detections_enabled;
        for (const auto& ice : options.ice_servers) {
            IceServerConfig server;
            server.urls = ice.urls;
            server.username = ice.username;
            server.credential = ice.credential;
            config.ice_servers.push_back(std::move(server));
        }
        return config;
    }

    bool ensure_video_subscription()
    {
        std::shared_ptr<media::MediaSource> media_ref;
        {
            std::lock_guard lock(mutex);
            if (video_sub != 0) {
                return true;
            }
            media_ref = media;
        }
        if (!media_ref) {
            return false;
        }
        const int preview = options.preview_stream_id;
        const uint64_t subscription = media_ref->subscribe(
            preview,
            [this](std::shared_ptr<const media::MediaFrame> frame) {
                if (!frame) {
                    return;
                }
                if (frame->type() == media::MediaType::Audio) {
                    enqueue_audio(std::move(frame));
                } else if (frame->type() == media::MediaType::Video) {
                    enqueue_video(std::move(frame));
                }
            });
        if (!subscription) {
            return false;
        }
        {
            std::lock_guard lock(mutex);
            if (running.load() && video_sub == 0) {
                video_sub = subscription;
                return true;
            }
        }
        media_ref->unsubscribe(preview, subscription);
        return false;
    }

    void ensure_ai_subscriptions()
    {
        if (!options.detections_enabled) {
            return;
        }
        std::shared_ptr<media::MediaSource> media_ref;
        std::shared_ptr<media::DetectionHub> detection_ref;
        {
            std::lock_guard lock(mutex);
            if (ai_hold_sub != 0) {
                return;
            }
            media_ref = media;
            detection_ref = detections;
        }
        if (!media_ref || !detection_ref) {
            return;
        }

        const uint64_t detection_subscription = detection_ref->subscribe(
            [this](const media::DetectionFrame& detection) {
                enqueue_detection(detection);
            });
        const uint64_t ai_subscription = media_ref->subscribe(
            2, [](std::shared_ptr<const media::MediaFrame>) {});
        if (!ai_subscription) {
            if (detection_subscription) {
                detection_ref->unsubscribe(detection_subscription);
            }
            std::fprintf(stderr, "[webrtc] AI/YOLO subscribe failed\n");
            return;
        }

        bool keep = false;
        {
            std::lock_guard lock(mutex);
            const bool any_connected = std::any_of(
                viewers.begin(), viewers.end(),
                [](const auto& entry) {
                    return entry.second && entry.second->connected;
                });
            if (running.load() && any_connected && ai_hold_sub == 0) {
                ai_hold_sub = ai_subscription;
                detect_sub = detection_subscription;
                keep = true;
            }
        }
        if (!keep) {
            media_ref->unsubscribe(2, ai_subscription);
            if (detection_subscription) {
                detection_ref->unsubscribe(detection_subscription);
            }
        }
    }

    void enqueue_video(std::shared_ptr<const media::MediaFrame> frame)
    {
        if (!frame || !running.load()) {
            return;
        }
        bool request_keyframe = false;
        bool schedule = false;
        bool queued = false;
        {
            std::lock_guard lock(mutex);
            if (frame->keyframe()) {
                keyframe_pending = false;
            }
            for (auto& entry : viewers) {
                auto& viewer = entry.second;
                if (!viewer || !viewer->connected || !viewer->peer) {
                    continue;
                }
                if (frame->size() > video_queue_limit ||
                    viewer->video_queue.size() >= kVideoQueueFrames ||
                    viewer->video_bytes >
                        video_queue_limit - frame->size()) {
                    viewer->video_queue.clear();
                    viewer->video_bytes = 0;
                    if (!frame->keyframe()) {
                        if (!keyframe_pending) {
                            keyframe_pending = true;
                            request_keyframe = true;
                        }
                        continue;
                    }
                }
                viewer->video_bytes += frame->size();
                viewer->video_queue.push_back(frame);
                queued = true;
            }
            if (queued && !drain_scheduled) {
                drain_scheduled = true;
                schedule = true;
            }
        }
        if (request_keyframe) {
            std::shared_ptr<media::MediaSource> media_ref;
            {
                std::lock_guard lock(mutex);
                media_ref = media;
            }
            if (media_ref) {
                media_ref->request_keyframe(options.preview_stream_id);
            }
        }
        if (schedule && !post([this] { drain_queues(); })) {
            std::lock_guard lock(mutex);
            drain_scheduled = false;
        }
    }

    void enqueue_audio(std::shared_ptr<const media::MediaFrame> frame)
    {
        if (!frame || !running.load() ||
            frame->codec() != media::Codec::G711U) {
            return;
        }
        bool schedule = false;
        bool queued = false;
        {
            std::lock_guard lock(mutex);
            for (auto& entry : viewers) {
                auto& viewer = entry.second;
                if (!viewer || !viewer->connected || !viewer->peer) {
                    continue;
                }
                if (frame->size() > kAudioQueueBytes ||
                    viewer->audio_queue.size() >= kAudioQueueFrames ||
                    viewer->audio_bytes >
                        kAudioQueueBytes - frame->size()) {
                    viewer->audio_queue.clear();
                    viewer->audio_bytes = 0;
                }
                viewer->audio_bytes += frame->size();
                viewer->audio_queue.push_back(frame);
                queued = true;
            }
            if (queued && !drain_scheduled) {
                drain_scheduled = true;
                schedule = true;
            }
        }
        if (schedule && !post([this] { drain_queues(); })) {
            std::lock_guard lock(mutex);
            drain_scheduled = false;
        }
    }

    void enqueue_detection(const media::DetectionFrame& detection)
    {
        if (!running.load()) {
            return;
        }
        bool schedule = false;
        bool queued = false;
        {
            std::lock_guard lock(mutex);
            for (auto& entry : viewers) {
                auto& viewer = entry.second;
                if (viewer && viewer->connected && viewer->peer) {
                    viewer->latest_detection = detection;
                    queued = true;
                }
            }
            if (queued && !drain_scheduled) {
                drain_scheduled = true;
                schedule = true;
            }
        }
        if (schedule && !post([this] { drain_queues(); })) {
            std::lock_guard lock(mutex);
            drain_scheduled = false;
        }
    }

    void drain_queues()
    {
        struct Batch {
            std::shared_ptr<KvsPeerSession> peer;
            std::deque<std::shared_ptr<const media::MediaFrame>> video_frames;
            std::deque<std::shared_ptr<const media::MediaFrame>> audio_frames;
            std::optional<media::DetectionFrame> detection;
        };
        std::vector<Batch> batches;
        {
            std::lock_guard lock(mutex);
            batches.reserve(viewers.size());
            for (auto& entry : viewers) {
                auto& viewer = entry.second;
                if (!viewer || !viewer->peer) {
                    continue;
                }
                Batch batch;
                batch.peer = viewer->peer;
                batch.video_frames.swap(viewer->video_queue);
                batch.audio_frames.swap(viewer->audio_queue);
                viewer->video_bytes = 0;
                viewer->audio_bytes = 0;
                batch.detection.swap(viewer->latest_detection);
                batches.push_back(std::move(batch));
            }
            drain_scheduled = false;
        }

        for (auto& batch : batches) {
            if (!batch.peer) {
                continue;
            }
            for (const auto& frame : batch.video_frames) {
                (void)batch.peer->write_video_frame(frame);
            }
            for (const auto& frame : batch.audio_frames) {
                (void)batch.peer->write_audio_frame(frame);
            }
            if (batch.detection) {
                (void)batch.peer->send_detections(*batch.detection);
            }
        }
    }

    void handle_peer_state(
        const std::string& viewer_id,
        const std::weak_ptr<KvsPeerSession>& weak_peer,
        const std::string& state)
    {
        auto peer = weak_peer.lock();
        if (!peer) {
            return;
        }
        if (!ztk_poller_is_current_thread(poller)) {
            (void)post([this, viewer_id, weak_peer, state] {
                handle_peer_state(viewer_id, weak_peer, state);
            });
            return;
        }

        std::fprintf(stderr, "[webrtc] peer %s -> %s\n",
                     viewer_id.c_str(), state.c_str());
        if (state == "connected") {
            {
                std::lock_guard lock(mutex);
                const auto it = viewers.find(viewer_id);
                if (it == viewers.end() || !it->second ||
                    it->second->peer != peer) {
                    return;
                }
                it->second->connected = true;
            }
            ensure_ai_subscriptions();
            std::shared_ptr<media::MediaSource> media_ref;
            {
                std::lock_guard lock(mutex);
                media_ref = media;
            }
            if (media_ref) {
                media_ref->request_keyframe(options.preview_stream_id);
            }
            return;
        }
        if (state == "disconnected" || state == "failed" ||
            state == "closed") {
            remove_viewer(viewer_id, state.c_str());
        }
    }

    bool create_viewer(const std::string& viewer_id, const std::string& sdp)
    {
        if (viewer_id.empty() || sdp.empty() || !running.load()) {
            return false;
        }
        remove_viewer(viewer_id, "replace");
        {
            std::lock_guard lock(mutex);
            if (viewers.size() >= viewer_limit()) {
                std::fprintf(stderr,
                             "[webrtc] reject viewer=%s limit=%zu\n",
                             viewer_id.c_str(), viewer_limit());
                return false;
            }
        }
        if (!ensure_video_subscription()) {
            std::fprintf(stderr, "[webrtc] subscribe preview failed\n");
            return false;
        }

        auto peer =
            std::make_shared<KvsPeerSession>(
                peer_config());
        std::weak_ptr<KvsPeerSession> weak_peer = peer;

        peer->set_local_candidate_handler(
            [this, viewer_id](const std::string& candidate_json) {
                if (candidate_json.empty() || !running.load()) {
                    return;
                }
                std::string candidate;
                std::string mid;
                Json::Value candidate_message;
                std::string parse_error;
                if (parse_json_object(candidate_json, candidate_message, parse_error)) {
                    (void)get_optional_string(candidate_message, "candidate", candidate, parse_error);
                    (void)get_optional_string(candidate_message, "sdpMid", mid, parse_error);
                }
                std::shared_ptr<SignalingClient> signal;
                {
                    std::lock_guard lock(mutex);
                    signal = signaling;
                }
                if (signal && !candidate.empty()) {
                    signal->send_candidate(
                        candidate, mid.empty() ? "0" : mid, 0, viewer_id);
                }
            });
        peer->set_local_sdp_handler(
            [this, viewer_id](const std::string& type,
                              const std::string& local_sdp) {
                if (type != "answer" || !running.load()) {
                    return;
                }
                std::shared_ptr<SignalingClient> signal;
                {
                    std::lock_guard lock(mutex);
                    signal = signaling;
                }
                if (signal) {
                    (void)signal->send_answer(local_sdp, viewer_id);
                }
            });
        peer->set_keyframe_handler([this] {
            std::shared_ptr<media::MediaSource> media_ref;
            {
                std::lock_guard lock(mutex);
                media_ref = media;
            }
            if (media_ref) {
                media_ref->request_keyframe(options.preview_stream_id);
            }
        });
        peer->set_remote_audio_handler(
            [this](const uint8_t* data, size_t len) {
                std::shared_ptr<media::MediaSource> media_ref;
                {
                    std::lock_guard lock(mutex);
                    media_ref = media;
                }
                if (media_ref) {
                    (void)media_ref->play_g711u(data, len);
                }
            });
        peer->set_state_handler(
            [this, viewer_id, weak_peer](const std::string& state) {
                handle_peer_state(viewer_id, weak_peer, state);
            });

        if (!peer->start()) {
            queue_reap(std::move(peer));
            return false;
        }

        auto viewer = std::make_shared<ViewerSession>();
        viewer->viewer_id = viewer_id;
        viewer->peer = peer;
        {
            std::lock_guard lock(mutex);
            if (!running.load()) {
                queue_reap(std::move(peer));
                return false;
            }
            viewers.emplace(viewer_id, viewer);
        }

        if (!peer->handle_remote_offer(sdp)) {
            remove_viewer(viewer_id, "offer-failed");
            return false;
        }
        std::deque<PendingCandidate> queued_candidates;
        {
            std::lock_guard lock(mutex);
            const auto it = viewers.find(viewer_id);
            if (it != viewers.end() && it->second) {
                it->second->remote_description_ready = true;
            }
            const auto pending = pending_candidates.find(viewer_id);
            if (pending != pending_candidates.end()) {
                queued_candidates = std::move(pending->second);
                pending_candidates.erase(pending);
            }
        }
        for (const auto& queued : queued_candidates) {
            (void)peer->handle_remote_candidate(
                queued.candidate, queued.mid, 0);
        }
        std::fprintf(stderr, "[webrtc] answer sent viewer=%s active=%zu\n",
                     viewer_id.c_str(), active_viewers());
        return true;
    }

    std::size_t active_viewers()
    {
        std::lock_guard lock(mutex);
        return viewers.size();
    }

    void handle_candidate(const std::string& viewer_id,
                          const std::string& candidate,
                          const std::string& mid)
    {
        std::shared_ptr<ViewerSession> viewer;
        {
            std::lock_guard lock(mutex);
            auto it = viewers.find(viewer_id);
            if (it != viewers.end()) {
                viewer = it->second;
            } else if (viewer_id.empty() && viewers.size() == 1) {
                viewer = viewers.begin()->second;
            }
        }
        if (viewer && viewer->peer) {
            bool remote_description_ready = false;
            {
                std::lock_guard lock(mutex);
                remote_description_ready = viewer->remote_description_ready;
                if (!remote_description_ready && !candidate.empty()) {
                    auto& pending = pending_candidates[viewer_id];
                    if (pending.size() < kPendingCandidateLimit) {
                        pending.push_back({candidate, mid});
                    }
                }
            }
            if (remote_description_ready) {
                (void)viewer->peer->handle_remote_candidate(
                    candidate, mid, 0);
            }
            return;
        }
        if (!viewer_id.empty() && !candidate.empty()) {
            std::lock_guard lock(mutex);
            if (pending_candidates.size() < kPendingCandidateLimit) {
                auto& pending = pending_candidates[viewer_id];
                if (pending.size() < kPendingCandidateLimit) {
                    pending.push_back({candidate, mid});
                }
            }
        }
    }

    void remove_viewer(const std::string& viewer_id, const char* reason)
    {
        std::shared_ptr<ViewerSession> removed;
        uint64_t release_video = 0;
        uint64_t release_ai = 0;
        uint64_t release_detection = 0;
        std::shared_ptr<media::MediaSource> media_ref;
        std::shared_ptr<media::DetectionHub> detection_ref;
        {
            std::lock_guard lock(mutex);
            const auto it = viewers.find(viewer_id);
            if (it == viewers.end()) {
                return;
            }
            removed = std::move(it->second);
            viewers.erase(it);
            pending_candidates.erase(viewer_id);
            media_ref = media;
            detection_ref = detections;

            const bool any_connected = std::any_of(
                viewers.begin(), viewers.end(),
                [](const auto& entry) {
                    return entry.second && entry.second->connected;
                });
            if (!any_connected) {
                release_ai = ai_hold_sub;
                ai_hold_sub = 0;
                release_detection = detect_sub;
                detect_sub = 0;
            }
            if (viewers.empty()) {
                release_video = video_sub;
                video_sub = 0;
                drain_scheduled = false;
            }
        }

        if (release_detection && detection_ref) {
            detection_ref->unsubscribe(release_detection);
        }
        if (release_ai && media_ref) {
            media_ref->unsubscribe(2, release_ai);
        }
        if (release_video && media_ref) {
            media_ref->unsubscribe(
                options.preview_stream_id, release_video);
        }
        if (removed && removed->peer) {
            std::fprintf(stderr,
                         "[webrtc] remove viewer=%s reason=%s remain=%zu\n",
                         viewer_id.c_str(), reason ? reason : "unknown",
                         active_viewers());
            queue_reap(std::move(removed->peer));
        }
    }

    void remove_all_viewers(const char* reason)
    {
        std::vector<std::string> ids;
        {
            std::lock_guard lock(mutex);
            ids.reserve(viewers.size());
            for (const auto& entry : viewers) {
                ids.push_back(entry.first);
            }
        }
        for (const auto& id : ids) {
            remove_viewer(id, reason);
        }
    }

    void on_signal(const std::string& type, const std::string& json)
    {
        if (!running.load()) {
            return;
        }
        Json::Value message;
        std::string parse_error;
        if (!parse_json_object(json, message, parse_error)) {
            std::fprintf(stderr, "[webrtc] invalid signaling message: %s\n",
                         parse_error.c_str());
            return;
        }
        if (type == "peer-joined") {
            std::string role;
            std::string id;
            if (!get_optional_string(message, "role", role, parse_error) ||
                !get_optional_string(message, "clientId", id, parse_error)) {
                return;
            }
            if (role == "viewer" && !id.empty()) {
                std::fprintf(stderr, "[webrtc] viewer joined %s\n",
                             id.c_str());
            }
            return;
        }
        if (type == "peer-left") {
            std::string role;
            std::string id;
            if (!get_optional_string(message, "role", role, parse_error) ||
                !get_optional_string(message, "clientId", id, parse_error)) {
                return;
            }
            if (role == "viewer" && !id.empty()) {
                remove_viewer(id, "peer-left");
            }
            return;
        }
        if (type == "offer") {
            std::string sdp;
            std::string viewer_id;
            if (!get_required_string(message, "sdp", sdp, parse_error) ||
                !get_required_string(message, "from", viewer_id, parse_error)) {
                return;
            }
            if (sdp.find("ice-ufrag") == std::string::npos ||
                sdp.find("ice-pwd") == std::string::npos) {
                std::fprintf(stderr,
                             "[webrtc] invalid offer viewer=%s len=%zu\n",
                             viewer_id.c_str(), sdp.size());
                return;
            }
            if (!create_viewer(viewer_id, sdp)) {
                std::fprintf(stderr,
                             "[webrtc] setup failed viewer=%s\n",
                             viewer_id.c_str());
            }
            return;
        }
        if (type == "candidate") {
            std::string from;
            std::string candidate;
            std::string sdp_mid;
            if (!get_required_string(message, "from", from, parse_error) ||
                !get_required_string(message, "candidate", candidate, parse_error) ||
                !get_optional_string(message, "sdpMid", sdp_mid, parse_error)) {
                return;
            }
            handle_candidate(from, candidate, sdp_mid);
            return;
        }
        if (type == "signaling-disconnected") {
            remove_all_viewers("signaling-disconnected");
            return;
        }
        if (type == "error") {
            std::fprintf(stderr, "[webrtc] signaling error: %s\n",
                         json.c_str());
        }
    }
};

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
    if (impl_->running.load() || !context.media_source ||
        !context.poller_pool) {
        return false;
    }
    impl_->poller = ztk_poller_pool_get_first(context.poller_pool);
    if (!impl_->poller) {
        return false;
    }

    ztk_thread_pool_opts_t cleanup_options{};
    cleanup_options.thread_count = 1;
    cleanup_options.priority = ZTK_THREAD_PRIO_NORMAL;
    cleanup_options.auto_start = 1;
    impl_->cleanup_pool = ztk_thread_pool_create(&cleanup_options);
    if (!impl_->cleanup_pool) {
        return false;
    }

    impl_->media = context.media_source;
    impl_->detections = context.detections;
    if (context.pending_frame_bytes_per_session > 0) {
        impl_->video_queue_limit = std::min(
            kVideoQueueBytes,
            context.pending_frame_bytes_per_session);
    }
    impl_->running = true;

    SignalingConfig config;
    config.url = impl_->options.signaling_url;
    config.token = impl_->options.signaling_token;
    config.room = impl_->options.room;
    config.role = "master";
    auto signaling =
        std::make_shared<SignalingClient>(
            config, impl_->poller);
    signaling->set_handler(
        [this](const std::string& type, const std::string& json) {
            impl_->on_signal(type, json);
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
            impl_->detections.reset();
        }
        ztk_thread_pool_destroy(impl_->cleanup_pool);
        impl_->cleanup_pool = nullptr;
        impl_->poller = nullptr;
        return false;
    }

    {
        std::lock_guard lock(impl_->mutex);
        impl_->pending_candidates.clear();
    }
    std::fprintf(stderr,
                 "[webrtc] plugin started room=%s max_viewers=%zu "
                 "poller=shared kvs=%s\n",
                 impl_->options.room.c_str(), impl_->viewer_limit(),
                 kvs_runtime_available()
                     ? "on" : "stub");
    return true;
}

void WebRtcPlugin::stop()
{
    if (!impl_->running.exchange(false)) {
        return;
    }

    std::shared_ptr<SignalingClient> signaling;
    {
        std::lock_guard lock(impl_->mutex);
        signaling = impl_->signaling;
    }
    if (signaling) {
        signaling->set_handler(nullptr);
        signaling->stop();
    }

    (void)impl_->call_poller_sync(
        [this] { impl_->remove_all_viewers("plugin-stop"); });
    (void)impl_->call_poller_sync([] {});

    {
        std::lock_guard lock(impl_->mutex);
        impl_->signaling.reset();
        impl_->media.reset();
        impl_->detections.reset();
        impl_->pending_candidates.clear();
    }
    if (impl_->cleanup_pool) {
        ztk_thread_pool_destroy(impl_->cleanup_pool);
        impl_->cleanup_pool = nullptr;
    }
    impl_->poller = nullptr;
    std::fprintf(stderr, "[webrtc] stopped\n");
}

} // namespace zero_ipc::protocol
