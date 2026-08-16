#pragma once

#include "hls_playlist.h"
#include "i_cmaf_muxer.h"
#include "i_segment_policy.h"
#include "i_segment_uploader.h"
#include "media/media_source.h"
#include "record_config.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace ipc_mini::record {

/**
 * One stream_id recording session.
 * subscribe → bounded queue → writer thread → CMAF + HLS (never block venc).
 */
class SegmentRecorder final {
public:
    SegmentRecorder(RecordConfig config,
                    std::shared_ptr<media::MediaSource> media_source,
                    std::unique_ptr<ICmafMuxer> muxer,
                    std::unique_ptr<ISegmentPolicy> policy,
                    std::unique_ptr<ISegmentUploader> uploader);
    ~SegmentRecorder();

    SegmentRecorder(const SegmentRecorder&) = delete;
    SegmentRecorder& operator=(const SegmentRecorder&) = delete;

    bool start();
    void stop();

private:
    void on_frame(std::shared_ptr<const media::MediaFrame> frame);
    void writer_loop();
    void process_frame(const std::shared_ptr<const media::MediaFrame>& frame);
    bool capture_formats_from_keyframe(
        const std::shared_ptr<const media::MediaFrame>& keyframe);
    bool ensure_session(
        const std::shared_ptr<const media::MediaFrame>& keyframe);
    bool open_next_segment(
        const std::shared_ptr<const media::MediaFrame>& keyframe);
    bool rotate_to_new_segment(
        const std::shared_ptr<const media::MediaFrame>& keyframe);
    void close_current_segment(bool end_playlist);
    bool make_day_paths(std::string& day_dir, std::string& stamp) const;
    std::string next_media_path(const std::string& day_dir,
                                const std::string& stamp) const;
    void upload_relative(const std::string& absolute_path);

    RecordConfig config_;
    std::shared_ptr<media::MediaSource> media_source_;
    std::unique_ptr<ICmafMuxer> muxer_;
    std::unique_ptr<ISegmentPolicy> policy_;
    std::unique_ptr<ISegmentUploader> uploader_;
    HlsPlaylist playlist_;

    uint64_t subscription_id_{0};
    std::thread writer_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<std::shared_ptr<const media::MediaFrame>> queue_;
    std::atomic<bool> stopping_{false};
    bool drop_logged_{false};

    media::VideoFormat video_format_{};
    std::optional<media::AudioFormat> audio_format_;
    bool formats_ready_{false};
    bool await_keyframe_{true};
    bool audio_config_ready_{true};
    bool session_ready_{false};
    std::shared_ptr<const media::MediaFrame> pending_keyframe_;

    std::string current_day_;
    std::string current_media_path_;
    int64_t segment_start_pts_ms_{-1};
    int64_t segment_last_pts_ms_{-1};
};

} // namespace ipc_mini::record
