#pragma once

#include "protocol/webrtc_plugin.h"

#include "webrtc_peer_connection.h"
#include "signaling_client.h"
#include "signaling_json.h"

#include <ztk/poller/poller.h>
#include <ztk/poller/poller_pool.h>
#include <ztk/thread/thread_pool.h>

#include <json/json.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ipc_mini::protocol {
namespace webrtc_detail {

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

void run_sync_call(void* user);
ztk_thread_pool* create_worker_pool();

} // namespace webrtc_detail

struct WebRtcPlugin::Impl {
    struct PendingCandidate {
        std::string candidate;
        std::string mid;
    };

    struct ViewerSession {
        std::string viewer_id;
        std::shared_ptr<ipc_mini::webrtc_net::WebRtcPeerConnection> peer;
        bool remote_description_ready{false};
        std::deque<std::shared_ptr<const media::MediaFrame>> video_queue;
        std::deque<std::shared_ptr<const media::MediaFrame>> audio_queue;
        std::size_t video_bytes{0};
        std::size_t audio_bytes{0};
        std::optional<media::DetectionResult> latest_detection;
        bool connected{false};
    };

    struct AsyncTask {
        Impl* self{nullptr};
        std::function<void()> fn;
    };

    struct ReapTask {
        std::shared_ptr<ipc_mini::webrtc_net::WebRtcPeerConnection> peer;
    };

    WebRtcPluginOptions options;
    std::shared_ptr<media::MediaSource> media;
    std::shared_ptr<media::DetectionSource> detection_source;
    std::shared_ptr<ipc_mini::webrtc_net::SignalingClient> signaling;

    ztk_poller_pool* signaling_pool{nullptr};
    ztk_poller* signaling_poller{nullptr};
    ztk_thread_pool* media_pool{nullptr};
    ztk_thread_pool* cleanup_pool{nullptr};

    std::mutex mutex;
    std::map<std::string, std::shared_ptr<ViewerSession>> viewers;
    std::map<std::string, std::deque<PendingCandidate>> pending_candidates;
    uint64_t video_sub{0};
    uint64_t ai_hold_sub{0};
    uint64_t detect_sub{0};
    std::size_t video_queue_limit{webrtc_detail::kVideoQueueBytes};
    bool drain_scheduled{false};
    bool keyframe_pending{false};
    std::atomic<bool> running{false};

    static void async_task(void* user);
    static void reap_task(void* user);

    bool post_signaling(std::function<void()> fn);
    bool post_media(std::function<void()> fn);
    bool call_signaling_sync(std::function<void()> fn);
    bool call_media_sync(std::function<void()> fn);
    bool start_runtime();
    void destroy_runtime();
    void queue_reap(
        std::shared_ptr<ipc_mini::webrtc_net::WebRtcPeerConnection> peer);
    std::size_t viewer_limit() const;
    ipc_mini::webrtc_net::PeerConnectionConfig peer_config() const;

    void on_signaling_message(const std::string& type, const std::string& json);
    void on_offer(const Json::Value& message, std::string& parse_error);
    void on_candidate_message(const Json::Value& message,
                              std::string& parse_error);
    void on_peer_left(const Json::Value& message, std::string& parse_error);
    void on_signaling_disconnected();

    bool create_viewer(const std::string& viewer_id, const std::string& sdp);
    void on_peer_state(
        const std::string& viewer_id,
        const std::weak_ptr<ipc_mini::webrtc_net::WebRtcPeerConnection>& weak_peer,
        const std::string& state);
    void on_connected(
        const std::string& viewer_id,
        const std::shared_ptr<ipc_mini::webrtc_net::WebRtcPeerConnection>& peer);
    void apply_remote_candidate(const std::string& viewer_id,
                                const std::string& candidate,
                                const std::string& mid);
    std::size_t active_viewers();
    void remove_viewer(const std::string& viewer_id, const char* reason);
    void remove_all_viewers(const char* reason);

    bool start_uplink();
    void start_ai_uplink();
    void request_preview_keyframe();
    void schedule_drain();
    void enqueue_video(std::shared_ptr<const media::MediaFrame> frame);
    void enqueue_audio(std::shared_ptr<const media::MediaFrame> frame);
    void enqueue_detection(const media::DetectionResult& detection);
    void drain();
};

} // namespace ipc_mini::protocol
