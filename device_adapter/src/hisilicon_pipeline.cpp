#include "hisilicon_pipeline.h"

#include "ai_stream_publisher.h"
#include "dev_osd.h"
#include "dev_svp.h"
#include "dev_svp_yolov8.h"
#include "font_renderer.h"
#include "media/detection.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace zero_ipc::device_adapter {
namespace {

constexpr int kAiDefaultWidth = 640;
constexpr int kAiDefaultHeight = 640;
constexpr int kAiDefaultFps = 1;
constexpr int kAiIdleStopSeconds = 5;

media::Codec audio_codec_from_name(const std::string& name)
{
    if (name == "G711U") {
        return media::Codec::G711U;
    }
    if (name == "AAC") {
        return media::Codec::AAC;
    }
    return media::Codec::Unknown;
}

} // namespace

HisiliconPipeline::HisiliconPipeline(HisiliconPipelineOptions options)
    : options_(std::move(options))
{
}

HisiliconPipeline::~HisiliconPipeline()
{
    stop();
}

bool HisiliconPipeline::start(const std::shared_ptr<media::MediaSource>& source)
{
    if (running_ || !source) {
        return false;
    }

    if (!hisilicon::dev::chn::init(options_.sys_flag, options_.lane_mode,
                                   options_.sensor_width, options_.sensor_height,
                                   options_.sensor_fps, options_.wdr_mode)) {
        return false;
    }
    sys_inited_ = true;

    const bool need_osd =
        options_.time_osd_enabled || options_.yolov8_enabled;
    if (need_osd) {
        if (!options_.font_path.empty()) {
            g_freetype.set_font_path(options_.font_path.c_str());
        }
        if (!hisilicon::dev::osd::init()) {
            // Missing font must not block WebRTC; overlays stay off.
            std::fprintf(stderr,
                         "[hisi] osd/freetype init failed (font=%s) — "
                         "continue without OSD\n",
                         options_.font_path.empty()
                             ? "(default)"
                             : options_.font_path.c_str());
            options_.time_osd_enabled = false;
            osd_inited_ = false;
        } else {
            osd_inited_ = true;
        }
    }

    encoded_channel_ = std::make_shared<EncodedStreamChannel>(
        options_.sensor_name.c_str(), options_.encoder_mode.c_str(),
        options_.channel_id, source);
    const media::Codec codec =
        std::string(options_.encoder_mode).find("H265") != std::string::npos
            ? media::Codec::H265
            : media::Codec::H264;
    hisilicon::dev::time_osd_options time_osd;
    time_osd.enable = options_.time_osd_enabled && osd_inited_;
    time_osd.x = options_.time_osd_x;
    time_osd.y = options_.time_osd_y;
    time_osd.font_size = options_.time_osd_font_size;

    auto fail_cleanup = [this]() {
        stop_yolov8_locked();
        ai_publisher_.reset();
        hisilicon::dev::chn::enable_audio_play(false);
        hisilicon::dev::chn::enable_audio(false, false, nullptr);
        if (svp_inited_) {
            hisilicon::dev::svp::release();
            svp_inited_ = false;
        }
        if (encoded_channel_) {
            encoded_channel_->stop();
            encoded_channel_.reset();
        }
        if (osd_inited_) {
            hisilicon::dev::osd::release();
            osd_inited_ = false;
        }
        if (sys_inited_) {
            hisilicon::dev::chn::release();
            sys_inited_ = false;
        }
    };

    if (!encoded_channel_->register_video_tracks(
            options_.video_width, options_.video_height, options_.video_fps, codec) ||
        !encoded_channel_->start(
            options_.video_width, options_.video_height, options_.video_fps,
            options_.bitrate_kbps, options_.svc_enabled, time_osd)) {
        fail_cleanup();
        return false;
    }

    if (options_.audio_enabled) {
        const media::Codec audio_codec =
            audio_codec_from_name(options_.audio_codec);
        const int sample_rate =
            audio_codec == media::Codec::AAC ? 44100 : 8000;
        const int channels =
            audio_codec == media::Codec::AAC ? 2 : 1;
        if (!encoded_channel_->register_audio_tracks(
                audio_codec, sample_rate, channels, 16) ||
            !hisilicon::dev::chn::enable_audio(
                true, options_.audio_microphone,
                options_.audio_codec.c_str())) {
            fail_cleanup();
            return false;
        }
        /*
         * Do NOT start AO/ADEC here. On inner-codec boards, enabling AO at boot
         * can starve AI capture and kill uplink audio. Speaker starts lazily on
         * first remote G711U frame (chn::play_g711u → aplay::start).
         */
        if (audio_codec == media::Codec::G711U) {
            std::fprintf(stderr,
                         "[hisi] audio capture G711U on; speaker waits for talk\n");
        }
    }

    if (options_.yolov8_enabled) {
        if (options_.yolov8_config_file.empty() ||
            options_.yolov8_model_file.empty() ||
            !hisilicon::dev::svp::init(options_.yolov8_config_file.c_str())) {
            fail_cleanup();
            return false;
        }
        svp_inited_ = true;

        ai_publisher_ = std::make_shared<AiStreamPublisher>(source);
        if (!ai_publisher_->register_track(
                kAiDefaultWidth, kAiDefaultHeight, kAiDefaultFps)) {
            fail_cleanup();
            return false;
        }
    }

    hisilicon::dev::chn::start_capture(true);
    /* Main encoding is permanent; secondary/AI encoding starts on demand. */
    hisilicon::dev::chn::request_i_frame(options_.channel_id, 0);
    {
        std::lock_guard lock(stream_mutex_);
        stream_worker_stop_ = false;
        ai_worker_stop_ = false;
        sub_stream_required_ = false;
        ai_stream_required_ = false;
        ++stream_generation_;
        ++ai_generation_;
        running_ = true;
    }
    stream_worker_ =
        std::thread(&HisiliconPipeline::stream_lifecycle_worker, this);
    if (options_.yolov8_enabled) {
        ai_worker_ =
            std::thread(&HisiliconPipeline::ai_lifecycle_worker, this);
    }
    return true;
}

void HisiliconPipeline::stop()
{
    if (!running_ && !encoded_channel_ && !sys_inited_) {
        return;
    }
    {
        std::lock_guard lock(stream_mutex_);
        running_ = false;
        stream_worker_stop_ = true;
        ai_worker_stop_ = true;
        sub_stream_required_ = false;
        ai_stream_required_ = false;
        ++stream_generation_;
        ++ai_generation_;
    }
    stream_cv_.notify_all();
    ai_cv_.notify_all();
    if (stream_worker_.joinable()) {
        stream_worker_.join();
    }
    if (ai_worker_.joinable()) {
        ai_worker_.join();
    }

    {
        std::lock_guard lock(stream_mutex_);
        stop_yolov8_locked();
    }
    ai_publisher_.reset();

    hisilicon::dev::chn::start_capture(false);
    hisilicon::dev::chn::enable_audio_play(false);
    hisilicon::dev::chn::enable_audio(false, false, nullptr);
    if (encoded_channel_) {
        encoded_channel_->stop();
        encoded_channel_.reset();
    }
    if (svp_inited_) {
        hisilicon::dev::svp::release();
        svp_inited_ = false;
    }
    if (osd_inited_) {
        hisilicon::dev::osd::release();
        osd_inited_ = false;
    }
    if (sys_inited_) {
        hisilicon::dev::chn::release();
        sys_inited_ = false;
    }
}

bool HisiliconPipeline::request_keyframe(int channel_id, int stream_id)
{
    return hisilicon::dev::chn::request_i_frame(channel_id, stream_id);
}

bool HisiliconPipeline::play_g711u(const uint8_t* data, size_t len)
{
    return hisilicon::dev::chn::play_g711u(data, len);
}

void HisiliconPipeline::set_detection_source(
    std::shared_ptr<media::DetectionSource> source)
{
    std::lock_guard lock(stream_mutex_);
    detection_source_ = std::move(source);
}

bool HisiliconPipeline::set_stream_active(
    int channel_id, int stream_id, bool active)
{
    if (channel_id != options_.channel_id) {
        return false;
    }
    if (stream_id == 0) {
        return true;
    }
    if (stream_id == AI_STREAM_ID) {
        if (!options_.yolov8_enabled) {
            return false;
        }
        std::lock_guard lock(stream_mutex_);
        if (!running_ || !encoded_channel_ || !ai_publisher_) {
            return false;
        }
        /*
         * Do NOT load/start YOLO here. subscribe(2) runs on the WebRTC
         * connected path; model load would block video uplink for seconds.
         * ai_lifecycle_worker starts/stops YOLO asynchronously.
         */
        ai_stream_required_ = active;
        ++ai_generation_;
        ai_cv_.notify_all();
        return true;
    }
    if (stream_id != SUB_STREAM_ID) {
        return false;
    }

    std::lock_guard lock(stream_mutex_);
    if (!running_ || !encoded_channel_) {
        return false;
    }
    sub_stream_required_ = active;
    ++stream_generation_;
    stream_cv_.notify_all();
    if (!active) {
        return true;
    }
    if (encoded_channel_->set_sub_stream_enabled(true)) {
        return true;
    }
    sub_stream_required_ = false;
    ++stream_generation_;
    stream_cv_.notify_all();
    return false;
}

void HisiliconPipeline::stream_lifecycle_worker()
{
    std::unique_lock lock(stream_mutex_);
    while (!stream_worker_stop_) {
        stream_cv_.wait(lock, [this] {
            return stream_worker_stop_ || !sub_stream_required_;
        });
        if (stream_worker_stop_) {
            break;
        }
        const uint64_t generation = stream_generation_;
        const bool changed = stream_cv_.wait_for(
            lock, std::chrono::seconds(kAiIdleStopSeconds),
            [this, generation] {
                return stream_worker_stop_ || sub_stream_required_ ||
                    stream_generation_ != generation;
            });
        if (changed || stream_worker_stop_ || sub_stream_required_) {
            continue;
        }
        if (encoded_channel_) {
            encoded_channel_->set_sub_stream_enabled(false);
        }
        stream_cv_.wait(lock, [this] {
            return stream_worker_stop_ || sub_stream_required_;
        });
    }
}

void HisiliconPipeline::ai_lifecycle_worker()
{
    std::unique_lock lock(stream_mutex_);
    while (!ai_worker_stop_) {
        ai_cv_.wait(lock, [this] {
            return ai_worker_stop_ ||
                (ai_stream_required_ && !yolov8_) ||
                (!ai_stream_required_ && static_cast<bool>(yolov8_));
        });
        if (ai_worker_stop_) {
            break;
        }

        if (ai_stream_required_ && !yolov8_) {
            const uint64_t generation = ai_generation_;
            std::printf("[zero_mini] yolov8 starting in background...\n");
            std::fflush(stdout);
            lock.unlock();
            const bool ok = start_yolov8_async(generation);
            lock.lock();
            if (ok) {
                std::printf("[zero_mini] yolov8 ready\n");
                std::fflush(stdout);
            } else if (ai_stream_required_ && ai_generation_ == generation &&
                       !yolov8_) {
                std::fprintf(stderr,
                             "[zero_mini] yolov8 start failed; preview keeps "
                             "running without detections\n");
                ai_stream_required_ = false;
                ++ai_generation_;
            }
            continue;
        }

        if (!ai_stream_required_ && yolov8_) {
            const uint64_t generation = ai_generation_;
            const bool changed = ai_cv_.wait_for(
                lock, std::chrono::seconds(kAiIdleStopSeconds),
                [this, generation] {
                    return ai_worker_stop_ || ai_stream_required_ ||
                        ai_generation_ != generation;
                });
            if (changed || ai_worker_stop_ || ai_stream_required_) {
                continue;
            }
            stop_yolov8_locked();
        }
    }
}

bool HisiliconPipeline::start_yolov8_async(uint64_t generation)
{
    std::shared_ptr<EncodedStreamChannel> channel;
    std::shared_ptr<AiStreamPublisher> publisher;
    std::shared_ptr<media::DetectionSource> source;
    std::string model_file;
    int channel_id = 0;
    {
        std::lock_guard lock(stream_mutex_);
        if (ai_worker_stop_ || !ai_stream_required_ ||
            ai_generation_ != generation || yolov8_ || !encoded_channel_ ||
            !ai_publisher_ || !svp_inited_) {
            return false;
        }
        channel = encoded_channel_;
        publisher = ai_publisher_;
        source = detection_source_;
        model_file = options_.yolov8_model_file;
        channel_id = options_.channel_id;
    }

    auto vi = channel->video_input();
    if (!vi) {
        return false;
    }

    auto detector = std::make_shared<hisilicon::dev::yolov8>(
        channel_id, AI_STREAM_ID, vi, model_file.c_str());
    if (source) {
        detector->set_detection_hook(
            [source](const hisilicon::dev::svp_npu_rect_info_t& info) {
                media::DetectionResult result;
                result.pts_ms = 0;
                result.frame_width = 640;
                result.frame_height = 640;
                const td_u16 n =
                    std::min<td_u16>(info.num, SVP_RECT_NUM);
                result.boxes.reserve(n);
                for (td_u16 i = 0; i < n; ++i) {
                    const auto& r = info.rect[i];
                    media::DetectionBox box;
                    box.class_id = r.class_id;
                    box.score = r.score;
                    const float x0 = static_cast<float>(r.point[0].x);
                    const float y0 = static_cast<float>(r.point[0].y);
                    const float x1 = static_cast<float>(r.point[2].x);
                    const float y1 = static_cast<float>(r.point[2].y);
                    box.x = x0 / 640.f;
                    box.y = y0 / 640.f;
                    box.w = (x1 - x0) / 640.f;
                    box.h = (y1 - y0) / 640.f;
                    result.boxes.push_back(box);
                }
                source->publish(std::move(result));
            });
    }

    /* Model load + VPSS/NPU setup — may take seconds; mutex not held. */
    if (!detector->start(channel->svc_venc_chn())) {
        return false;
    }

    const int width = detector->venc_w();
    const int height = detector->venc_h();
    {
        std::lock_guard lock(stream_mutex_);
        if (ai_worker_stop_ || !ai_stream_required_ ||
            ai_generation_ != generation || yolov8_) {
            detector->stop();
            return false;
        }
        if (width > 0 && height > 0) {
            publisher->register_track(width, height, kAiDefaultFps);
        }
        detector->register_stream_observer(publisher);
        yolov8_ = std::move(detector);
    }
    return true;
}

void HisiliconPipeline::stop_yolov8_locked()
{
    if (!yolov8_) {
        return;
    }
    if (ai_publisher_) {
        yolov8_->unregister_stream_observer(ai_publisher_);
    }
    yolov8_->stop();
    yolov8_.reset();
}

} // namespace zero_ipc::device_adapter
