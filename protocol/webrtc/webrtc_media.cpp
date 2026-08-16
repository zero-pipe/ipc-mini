#include "webrtc_plugin_impl.h"

#include <algorithm>
#include <cstdio>

namespace zero_ipc::protocol {

bool WebRtcPlugin::Impl::start_uplink()
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

void WebRtcPlugin::Impl::start_ai_uplink()
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

void WebRtcPlugin::Impl::request_preview_keyframe()
{
    std::shared_ptr<media::MediaSource> media_ref;
    {
        std::lock_guard lock(mutex);
        media_ref = media;
    }
    if (media_ref) {
        media_ref->request_keyframe(options.preview_stream_id);
    }
}

void WebRtcPlugin::Impl::schedule_drain()
{
    if (!post_media([this] { drain(); })) {
        std::lock_guard lock(mutex);
        drain_scheduled = false;
    }
}

void WebRtcPlugin::Impl::enqueue_video(
    std::shared_ptr<const media::MediaFrame> frame)
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
                viewer->video_queue.size() >= webrtc_detail::kVideoQueueFrames ||
                viewer->video_bytes > video_queue_limit - frame->size()) {
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
        request_preview_keyframe();
    }
    if (schedule) {
        schedule_drain();
    }
}

void WebRtcPlugin::Impl::enqueue_audio(
    std::shared_ptr<const media::MediaFrame> frame)
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
            if (frame->size() > webrtc_detail::kAudioQueueBytes ||
                viewer->audio_queue.size() >= webrtc_detail::kAudioQueueFrames ||
                viewer->audio_bytes >
                    webrtc_detail::kAudioQueueBytes - frame->size()) {
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
    if (schedule) {
        schedule_drain();
    }
}

void WebRtcPlugin::Impl::enqueue_detection(
    const media::DetectionFrame& detection)
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
    if (schedule) {
        schedule_drain();
    }
}

void WebRtcPlugin::Impl::drain()
{
    struct Batch {
        std::shared_ptr<zero_mini::webrtc_net::WebRtcPeerConnection> peer;
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

} // namespace zero_ipc::protocol
