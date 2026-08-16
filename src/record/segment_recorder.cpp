#include "record/segment_recorder.h"

#include "record/path_util.h"

#include <mpeg4-aac.h>
#include <mpeg4-avc.h>
#include <mpeg4-hevc.h>

#include <cstdio>
#include <ctime>
#include <sys/stat.h>

namespace ipc_mini::record {
namespace {

bool file_exists(const std::string& path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

std::string filename_of(const std::string& path)
{
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

SegmentRecorder::SegmentRecorder(
    RecordConfig config, std::shared_ptr<media::MediaSource> media_source,
    std::unique_ptr<ICmafMuxer> muxer, std::unique_ptr<ISegmentPolicy> policy,
    std::unique_ptr<ISegmentUploader> uploader)
    : config_(std::move(config)),
      media_source_(std::move(media_source)),
      muxer_(std::move(muxer)),
      policy_(std::move(policy)),
      uploader_(std::move(uploader))
{
}

SegmentRecorder::~SegmentRecorder()
{
    stop();
}

bool SegmentRecorder::start()
{
    if (!media_source_ || !muxer_ || !policy_ || !uploader_ ||
        writer_.joinable()) {
        return false;
    }
    if (!mkdir_p(config_.directory)) {
        std::fprintf(stderr, "[record] mkdir failed: %s\n",
                     config_.directory.c_str());
        return false;
    }

    stopping_.store(false);
    writer_ = std::thread([this] { writer_loop(); });

    subscription_id_ = media_source_->subscribe(
        config_.stream_id,
        [this](std::shared_ptr<const media::MediaFrame> frame) {
            on_frame(std::move(frame));
        });
    if (subscription_id_ == 0) {
        stopping_.store(true);
        queue_cv_.notify_all();
        writer_.join();
        return false;
    }

    media_source_->request_keyframe(config_.stream_id);
    std::printf("[record] start stream=%s(%d) audio=%s segment=%ds dir=%s%s\n",
                stream_tag(config_.stream_id), config_.stream_id,
                config_.audio ? "on" : "off", config_.segment_sec,
                config_.directory.c_str(),
                config_.upload_url.empty() ? "" : " upload=on");
    return true;
}

void SegmentRecorder::stop()
{
    if (subscription_id_ != 0 && media_source_) {
        media_source_->unsubscribe(config_.stream_id, subscription_id_);
        subscription_id_ = 0;
    }
    stopping_.store(true);
    queue_cv_.notify_all();
    if (writer_.joinable()) {
        writer_.join();
    }
    close_current_segment(true);
    if (muxer_) {
        muxer_->end_session();
    }
    session_ready_ = false;
    if (uploader_) {
        uploader_->stop();
    }
}

void SegmentRecorder::on_frame(std::shared_ptr<const media::MediaFrame> frame)
{
    if (!frame || stopping_.load()) {
        return;
    }
    if (!config_.audio && frame->type() == media::MediaType::Audio) {
        return;
    }

    std::lock_guard lock(queue_mutex_);
    if (queue_.size() >= config_.max_pending_frames) {
        if (!drop_logged_) {
            std::fprintf(stderr, "[record] queue full, dropping frames\n");
            drop_logged_ = true;
        }
        return;
    }
    queue_.push_back(std::move(frame));
    queue_cv_.notify_one();
}

void SegmentRecorder::writer_loop()
{
    while (true) {
        std::shared_ptr<const media::MediaFrame> frame;
        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return stopping_.load() || !queue_.empty();
            });
            if (queue_.empty()) {
                if (stopping_.load()) {
                    break;
                }
                continue;
            }
            frame = std::move(queue_.front());
            queue_.pop_front();
        }
        process_frame(frame);
    }
    close_current_segment(true);
    if (muxer_) {
        muxer_->end_session();
    }
    session_ready_ = false;
}

bool SegmentRecorder::capture_formats_from_keyframe(
    const std::shared_ptr<const media::MediaFrame>& keyframe)
{
    if (!keyframe || !keyframe->keyframe() ||
        keyframe->type() != media::MediaType::Video) {
        return false;
    }

    const auto video_track =
        media_source_->track(config_.stream_id, media::MediaType::Video);
    if (!video_track || !video_track->is_video()) {
        return false;
    }

    video_format_.codec = keyframe->codec();
    video_format_.width = video_track->video.width;
    video_format_.height = video_track->video.height;
    video_format_.frame_rate = video_track->video.frame_rate;
    video_format_.extradata.clear();

    if (keyframe->codec() == media::Codec::H264) {
        mpeg4_avc_t avc{};
        if (mpeg4_avc_from_nalu(keyframe->data(), keyframe->size(), &avc) < 0 ||
            avc.nb_sps == 0 || avc.nb_pps == 0) {
            return false;
        }
        video_format_.extradata.resize(256);
        const int bytes = mpeg4_avc_decoder_configuration_record_save(
            &avc, video_format_.extradata.data(),
            video_format_.extradata.size());
        if (bytes <= 0) {
            return false;
        }
        video_format_.extradata.resize(static_cast<std::size_t>(bytes));
    } else if (keyframe->codec() == media::Codec::H265) {
        mpeg4_hevc_t hevc{};
        if (mpeg4_hevc_from_nalu(keyframe->data(), keyframe->size(), &hevc) <
                0 ||
            hevc.numOfArrays == 0) {
            return false;
        }
        video_format_.extradata.resize(512);
        const int bytes = mpeg4_hevc_decoder_configuration_record_save(
            &hevc, video_format_.extradata.data(),
            video_format_.extradata.size());
        if (bytes <= 0) {
            return false;
        }
        video_format_.extradata.resize(static_cast<std::size_t>(bytes));
    } else {
        return false;
    }

    audio_format_.reset();
    audio_config_ready_ = true;
    if (config_.audio) {
        const auto audio_track =
            media_source_->track(config_.stream_id, media::MediaType::Audio);
        if (audio_track && audio_track->is_audio() &&
            (audio_track->audio.codec == media::Codec::AAC ||
             audio_track->audio.codec == media::Codec::G711U ||
             audio_track->audio.codec == media::Codec::G711A)) {
            audio_format_ = audio_track->audio;
            if (audio_format_->codec == media::Codec::AAC &&
                audio_format_->extradata.empty()) {
                audio_config_ready_ = false;
            }
        }
    }

    formats_ready_ = true;
    return true;
}

bool SegmentRecorder::make_day_paths(std::string& day_dir,
                                     std::string& stamp) const
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char day[32];
    char time[32];
    std::snprintf(day, sizeof(day), "%04d-%02d-%02d", local.tm_year + 1900,
                  local.tm_mon + 1, local.tm_mday);
    std::snprintf(time, sizeof(time), "%02d-%02d-%02d", local.tm_hour,
                  local.tm_min, local.tm_sec);
    day_dir = join_path(config_.directory, day);
    if (!mkdir_p(day_dir)) {
        return false;
    }
    stamp = std::string(day) + "_" + time;
    return true;
}

std::string SegmentRecorder::next_media_path(const std::string& day_dir,
                                             const std::string& stamp) const
{
    std::string path = join_path(day_dir, stamp + ".m4s");
    int suffix = 2;
    while (file_exists(path) && suffix < 100) {
        path = join_path(day_dir, stamp + "-" + std::to_string(suffix) + ".m4s");
        ++suffix;
    }
    return path;
}

void SegmentRecorder::upload_relative(const std::string& absolute_path)
{
    if (!uploader_ || config_.upload_url.empty() || absolute_path.empty()) {
        return;
    }
    const std::string prefix = config_.directory;
    std::string key = absolute_path;
    if (key.rfind(prefix, 0) == 0) {
        key = key.substr(prefix.size());
        if (!key.empty() && key.front() == '/') {
            key.erase(key.begin());
        }
    }
    if (!key.empty()) {
        uploader_->enqueue(absolute_path, key);
    }
}

void SegmentRecorder::close_current_segment(bool end_playlist)
{
    if (muxer_ && muxer_->segment_open()) {
        muxer_->close_segment();
        if (!current_media_path_.empty()) {
            double duration = 0.0;
            if (segment_start_pts_ms_ >= 0 &&
                segment_last_pts_ms_ >= segment_start_pts_ms_) {
                duration = static_cast<double>(
                               segment_last_pts_ms_ - segment_start_pts_ms_) /
                    1000.0;
            }
            if (duration < 0.001) {
                duration = static_cast<double>(config_.segment_sec);
            }
            playlist_.append(filename_of(current_media_path_), duration);
            upload_relative(current_media_path_);
            if (playlist_.active()) {
                upload_relative(playlist_.path());
            }
            std::printf("[record] segment close %s (%.1fs)\n",
                        current_media_path_.c_str(), duration);
        }
    }
    current_media_path_.clear();
    segment_start_pts_ms_ = -1;
    segment_last_pts_ms_ = -1;
    if (end_playlist && playlist_.active()) {
        playlist_.finish();
        upload_relative(playlist_.path());
    }
}

bool SegmentRecorder::ensure_session(
    const std::shared_ptr<const media::MediaFrame>& keyframe)
{
    if (session_ready_) {
        return true;
    }
    if (!formats_ready_ && !capture_formats_from_keyframe(keyframe)) {
        return false;
    }
    if (!audio_config_ready_) {
        return false;
    }

    std::string day_dir;
    std::string stamp;
    if (!make_day_paths(day_dir, stamp)) {
        return false;
    }
    const std::string init_path = join_path(day_dir, "init.mp4");
    if (!muxer_->start_session(video_format_, audio_format_, init_path)) {
        std::fprintf(stderr, "[record] init failed: %s\n", init_path.c_str());
        return false;
    }
    if (!playlist_.begin(join_path(day_dir, "index.m3u8"),
                         config_.segment_sec)) {
        muxer_->end_session();
        return false;
    }
    current_day_ = filename_of(day_dir);
    session_ready_ = true;
    upload_relative(init_path);
    upload_relative(playlist_.path());
    std::printf("[record] session init %s\n", init_path.c_str());
    return true;
}

bool SegmentRecorder::open_next_segment(
    const std::shared_ptr<const media::MediaFrame>& keyframe)
{
    std::string day_dir;
    std::string stamp;
    if (!make_day_paths(day_dir, stamp)) {
        return false;
    }
    const std::string day = filename_of(day_dir);
    if (session_ready_ && day != current_day_) {
        close_current_segment(true);
        muxer_->end_session();
        session_ready_ = false;
        current_day_.clear();
    }
    if (!ensure_session(keyframe)) {
        return false;
    }
    if (day != current_day_) {
        close_current_segment(true);
        const std::string init_path = join_path(day_dir, "init.mp4");
        if (!muxer_->start_session(video_format_, audio_format_, init_path)) {
            session_ready_ = false;
            return false;
        }
        playlist_.begin(join_path(day_dir, "index.m3u8"), config_.segment_sec);
        current_day_ = day;
        session_ready_ = true;
        upload_relative(init_path);
        upload_relative(playlist_.path());
    }

    const std::string path = next_media_path(day_dir, stamp);
    if (!muxer_->open_segment(path)) {
        std::fprintf(stderr, "[record] open failed: %s\n", path.c_str());
        return false;
    }
    current_media_path_ = path;
    segment_start_pts_ms_ = keyframe->pts_ms();
    segment_last_pts_ms_ = keyframe->pts_ms();
    policy_->on_segment_started(keyframe->pts_ms());
    std::printf("[record] segment open %s\n", path.c_str());
    return true;
}

bool SegmentRecorder::rotate_to_new_segment(
    const std::shared_ptr<const media::MediaFrame>& keyframe)
{
    close_current_segment(false);
    await_keyframe_ = false;
    if (!open_next_segment(keyframe)) {
        await_keyframe_ = true;
        return false;
    }
    const bool ok = muxer_->write_frame(keyframe);
    if (ok) {
        segment_last_pts_ms_ = keyframe->pts_ms();
    }
    return ok;
}

void SegmentRecorder::process_frame(
    const std::shared_ptr<const media::MediaFrame>& frame)
{
    if (!frame) {
        return;
    }

    if (frame->type() == media::MediaType::Audio) {
        if (!config_.audio) {
            return;
        }
        if (audio_format_ && audio_format_->codec == media::Codec::AAC &&
            audio_format_->extradata.empty() && frame->size() >= 7) {
            mpeg4_aac_t aac{};
            if (mpeg4_aac_adts_load(frame->data(), frame->size(), &aac) >= 7) {
                audio_format_->extradata.resize(32);
                const int bytes = mpeg4_aac_audio_specific_config_save(
                    &aac, audio_format_->extradata.data(),
                    audio_format_->extradata.size());
                if (bytes > 0) {
                    audio_format_->extradata.resize(
                        static_cast<std::size_t>(bytes));
                    audio_config_ready_ = true;
                    if (pending_keyframe_ && !muxer_->segment_open()) {
                        const auto key = pending_keyframe_;
                        pending_keyframe_.reset();
                        if (open_next_segment(key)) {
                            await_keyframe_ = false;
                            muxer_->write_frame(key);
                        }
                    }
                } else {
                    audio_format_->extradata.clear();
                }
            }
        }
        if (!muxer_->segment_open() || !audio_format_ ||
            (audio_format_->codec == media::Codec::AAC &&
             audio_format_->extradata.empty())) {
            return;
        }
        muxer_->write_frame(frame);
        return;
    }

    if (frame->type() != media::MediaType::Video) {
        return;
    }

    if (await_keyframe_) {
        if (!frame->keyframe()) {
            return;
        }
        if (!formats_ready_ && !capture_formats_from_keyframe(frame)) {
            return;
        }
        if (!audio_config_ready_) {
            pending_keyframe_ = frame;
            return;
        }
        if (!open_next_segment(frame)) {
            return;
        }
        pending_keyframe_.reset();
        await_keyframe_ = false;
        muxer_->write_frame(frame);
        segment_last_pts_ms_ = frame->pts_ms();
        return;
    }

    if (frame->keyframe() && policy_->should_rotate(frame)) {
        rotate_to_new_segment(frame);
        return;
    }

    if (!muxer_->segment_open()) {
        await_keyframe_ = true;
        return;
    }
    muxer_->write_frame(frame);
    segment_last_pts_ms_ = frame->pts_ms();
}

} // namespace ipc_mini::record
