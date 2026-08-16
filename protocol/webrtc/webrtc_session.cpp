#include "webrtc_plugin_impl.h"

#include <algorithm>
#include <cstdio>

namespace zero_ipc::protocol {
using zero_mini::webrtc_net::get_optional_string;
using zero_mini::webrtc_net::parse_json_object;

bool WebRtcPlugin::Impl::create_viewer(const std::string& viewer_id,
                                       const std::string& sdp)
{
    if (viewer_id.empty() || sdp.empty() || !running.load()) {
        return false;
    }
    remove_viewer(viewer_id, "replace");
    {
        std::lock_guard lock(mutex);
        if (viewers.size() >= viewer_limit()) {
            std::fprintf(stderr, "[webrtc] reject viewer=%s limit=%zu\n",
                         viewer_id.c_str(), viewer_limit());
            return false;
        }
    }
    if (!start_uplink()) {
        std::fprintf(stderr, "[webrtc] subscribe preview failed\n");
        return false;
    }

    auto peer = std::make_shared<zero_mini::webrtc_net::WebRtcPeerConnection>(
        peer_config());
    std::weak_ptr<zero_mini::webrtc_net::WebRtcPeerConnection> weak_peer = peer;

    peer->set_local_candidate_handler(
        [this, viewer_id](const std::string& candidate_json) {
            if (candidate_json.empty() || !running.load()) {
                return;
            }
            std::string candidate;
            std::string mid;
            Json::Value candidate_message;
            std::string parse_error;
            if (parse_json_object(candidate_json, candidate_message,
                                  parse_error)) {
                (void)get_optional_string(
                    candidate_message, "candidate", candidate, parse_error);
                (void)get_optional_string(
                    candidate_message, "sdpMid", mid, parse_error);
            }
            std::shared_ptr<zero_mini::webrtc_net::SignalingClient> signal;
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
            std::shared_ptr<zero_mini::webrtc_net::SignalingClient> signal;
            {
                std::lock_guard lock(mutex);
                signal = signaling;
            }
            if (signal) {
                (void)signal->send_answer(local_sdp, viewer_id);
            }
        });
    peer->set_keyframe_handler([this] {
        request_preview_keyframe();
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
            on_peer_state(viewer_id, weak_peer, state);
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
        (void)peer->handle_remote_candidate(queued.candidate, queued.mid, 0);
    }
    std::fprintf(stderr, "[webrtc] answer sent viewer=%s active=%zu\n",
                 viewer_id.c_str(), active_viewers());
    return true;
}

void WebRtcPlugin::Impl::on_peer_state(
    const std::string& viewer_id,
    const std::weak_ptr<zero_mini::webrtc_net::WebRtcPeerConnection>& weak_peer,
    const std::string& state)
{
    auto peer = weak_peer.lock();
    if (!peer) {
        return;
    }
    if (!signaling_poller ||
        !ztk_poller_is_current_thread(signaling_poller)) {
        (void)post_signaling([this, viewer_id, weak_peer, state] {
            on_peer_state(viewer_id, weak_peer, state);
        });
        return;
    }

    std::fprintf(stderr, "[webrtc] peer %s -> %s\n",
                 viewer_id.c_str(), state.c_str());
    if (state == "connected") {
        on_connected(viewer_id, peer);
        return;
    }
    if (state == "disconnected" || state == "failed" || state == "closed") {
        remove_viewer(viewer_id, state.c_str());
    }
}

void WebRtcPlugin::Impl::on_connected(
    const std::string& viewer_id,
    const std::shared_ptr<zero_mini::webrtc_net::WebRtcPeerConnection>& peer)
{
    {
        std::lock_guard lock(mutex);
        const auto it = viewers.find(viewer_id);
        if (it == viewers.end() || !it->second || it->second->peer != peer) {
            return;
        }
        it->second->connected = true;
    }
    start_ai_uplink();
    request_preview_keyframe();
}

void WebRtcPlugin::Impl::apply_remote_candidate(const std::string& viewer_id,
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
                if (pending.size() < webrtc_detail::kPendingCandidateLimit) {
                    pending.push_back({candidate, mid});
                }
            }
        }
        if (remote_description_ready) {
            (void)viewer->peer->handle_remote_candidate(candidate, mid, 0);
        }
        return;
    }
    if (!viewer_id.empty() && !candidate.empty()) {
        std::lock_guard lock(mutex);
        if (pending_candidates.size() < webrtc_detail::kPendingCandidateLimit) {
            auto& pending = pending_candidates[viewer_id];
            if (pending.size() < webrtc_detail::kPendingCandidateLimit) {
                pending.push_back({candidate, mid});
            }
        }
    }
}

std::size_t WebRtcPlugin::Impl::active_viewers()
{
    std::lock_guard lock(mutex);
    return viewers.size();
}

void WebRtcPlugin::Impl::remove_viewer(const std::string& viewer_id,
                                       const char* reason)
{
    std::shared_ptr<ViewerSession> removed;
    uint64_t release_video = 0;
    uint64_t release_ai = 0;
    uint64_t release_detection = 0;
    std::shared_ptr<media::MediaSource> media_ref;
    std::shared_ptr<media::DetectionSource> detection_ref;
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
        detection_ref = detection_source;

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
        media_ref->unsubscribe(options.preview_stream_id, release_video);
    }
    if (removed && removed->peer) {
        std::fprintf(stderr,
                     "[webrtc] remove viewer=%s reason=%s remain=%zu\n",
                     viewer_id.c_str(), reason ? reason : "unknown",
                     active_viewers());
        queue_reap(std::move(removed->peer));
    }
}

void WebRtcPlugin::Impl::remove_all_viewers(const char* reason)
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

} // namespace zero_ipc::protocol
