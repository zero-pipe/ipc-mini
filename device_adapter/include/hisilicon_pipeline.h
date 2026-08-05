#pragma once

#include "core/device_pipeline.h"
#include "media/detection_hub.h"
#include "encoded_stream_channel.h"
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace hisilicon::dev {
class yolov8;
}

namespace zero_ipc::device_adapter {

class AiStreamBridge;

struct HisiliconPipelineOptions {
    int32_t sys_flag{0};
    lane_divide_mode_t lane_mode{};
    /** Sensor/VI capture size �?must match sensor mode (SC4336P: 2560x1440@30). */
    int32_t sensor_width{2560};
    int32_t sensor_height{1440};
    int32_t sensor_fps{30};
    int32_t wdr_mode{0};
    std::string sensor_name{"SC4336P"};
    std::string encoder_mode{"H264_CBR"};
    int32_t channel_id{0};
    int32_t video_width{1920};
    int32_t video_height{1080};
    int32_t video_fps{25};
    int32_t bitrate_kbps{2048};
    int32_t svc_enabled{0};
    bool audio_enabled{false};
    bool audio_microphone{false};
    std::string audio_codec{"G711U"};
    bool time_osd_enabled{true};
    int32_t time_osd_x{32};
    int32_t time_osd_y{32};
    int32_t time_osd_font_size{32};
    /** Empty → keep FontRenderer default (/opt/zero_mini/fonts/...). */
    std::string font_path;
    bool yolov8_enabled{false};
    std::string yolov8_config_file;
    std::string yolov8_model_file;
};

/**
 * HiSilicon adapter implementing IDevicePipeline (DIP adapter).
 * Only place that may call chn / MPP APIs.
 */
class HisiliconPipeline final : public core::IDevicePipeline {
public:
    explicit HisiliconPipeline(HisiliconPipelineOptions options);
    ~HisiliconPipeline() override;

    bool start(const std::shared_ptr<media::MediaSource>& source) override;
    void stop() override;
    bool request_keyframe(int channel_id, int stream_id) override;
    bool set_stream_active(int channel_id, int stream_id,
                           bool active) override;
    bool play_g711u(const uint8_t* data, size_t len) override;
    void set_detection_hub(
        std::shared_ptr<media::DetectionHub> hub) override;

private:
    void stream_lifecycle_worker();
    void ai_lifecycle_worker();
    /** Load/start YOLO off the WebRTC path. generation must match ai_generation_. */
    bool start_yolov8_async(uint64_t generation);
    void stop_yolov8_locked();

    HisiliconPipelineOptions options_;
    std::shared_ptr<EncodedStreamChannel> encoded_channel_;
    std::shared_ptr<AiStreamBridge> ai_bridge_;
    std::shared_ptr<media::DetectionHub> detection_hub_;
    std::shared_ptr<hisilicon::dev::yolov8> yolov8_;
    std::mutex stream_mutex_;
    std::condition_variable stream_cv_;
    std::condition_variable ai_cv_;
    std::thread stream_worker_;
    std::thread ai_worker_;
    uint64_t stream_generation_{0};
    uint64_t ai_generation_{0};
    bool stream_worker_stop_{false};
    bool ai_worker_stop_{false};
    bool sub_stream_required_{false};
    bool ai_stream_required_{false};
    bool running_{false};
    bool sys_inited_{false};
    bool osd_inited_{false};
    bool svp_inited_{false};
};

} // namespace zero_ipc::device_adapter
