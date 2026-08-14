/**
 * pipeline_with_chn.cpp - 用一条直线理解 Hi3516CV610 的视频 Pipeline
 *
 * 这个文件是教学示例，故意不经过 Application、HisiliconPipeline、
 * MediaSource 和 WebRTC。它只展示最核心的一条链路：
 *
 *   Sensor
 *     -> MIPI 接收 RAW Bayer
 *     -> VI Pipe
 *     -> ISP（去马赛克、AE、AWB、降噪等）
 *     -> VI Channel 输出 YUV420
 *     -> VPSS
 *     -> VENC 编码 H.264
 *     -> 应用获取 H.264 NALU
 *     -> 写入 /tmp/pipeline_with_chn.h264
 *
 * 一个非常重要的概念：
 * 应用程序并不会循环 read() 摄像头 RAW，也不会自己把 RAW 转成 YUV。
 * 我们配置并启动各个硬件模块，再用 ss_mpi_sys_bind() 把它们连接起来；
 * 芯片内部会自动搬运 RAW/YUV。应用真正主动读取的是 VENC 编码后的码流。
 *
 * 本文件没有加入 board/Makefile，因为项目已经有 app/main.cpp，两个 main()
 * 不能同时链接。它应当作为独立教学入口编译，而不是和正式程序一起编译。
 */

// Same errno workaround as device_adapter/encoded_stream_channel.h:
// stream_observer.h declares on_stream_error(..., int32_t errno).
#include <cerrno>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

#pragma push_macro("errno")
#undef errno
#include <stream_observer.h>
#pragma pop_macro("errno")

#include "dev_chn.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <memory>
#include <thread>

namespace {

// -----------------------------------------------------------------------------
// 1. 为了让示例可以用 Ctrl+C 结束，只在信号函数里修改一个简单标志。
// ----------------------------------------------------------------------------

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int)
{
    g_stop = 1;
}

// -----------------------------------------------------------------------------
// 2. EncodedFileSink 是整条 Pipeline 最末端的“码流消费者”。
//
// dev::chn 已经封装了 MIPI、VI、ISP、VPSS 和 VENC 的配置与启动。
// 编码器取到一帧 H.264 后，会通过 stream_observer::on_stream_come()
// 通知我们。正式程序里的 EncodedStreamChannel 也是使用相同机制，只不过它
// 把码流复制成 MediaFrame，再 publish 到 MediaSource；本示例直接写文件。
// -----------------------------------------------------------------------------

class EncodedFileSink final : public hisilicon::dev::chn {
public:
    EncodedFileSink(const char* sensor_name,
                    const char* encoder_mode,
                    int channel_id,
                    const char* output_path)
        : hisilicon::dev::chn(sensor_name, encoder_mode, channel_id),
          output_path_(output_path)
    {
    }

    bool open_output()
    {
        output_ = std::fopen(output_path_, "wb");
        if (!output_) {
            std::perror("fopen");
            return false;
        }
        return true;
    }

    void close_output()
    {
        if (output_) {
            std::fclose(output_);
            output_ = nullptr;
        }
    }

    /**
     * 编码码流到达时会进入这里。
     *
     * sobj 中保存了：
     *   - chn()      ：摄像头/业务通道编号
     *   - stream_id()：0 主码流，1 子码流，2 AI 流
     *
     * head 中保存了这一帧的 NALU 列表。H.264 常见 NALU 包括：
     *   - SPS/PPS：解码参数
     *   - IDR：关键帧
     *   - P Slice：依赖前面图像的预测帧
     *
     * 注意：head->nalu[i].data 指向海思 VENC 的临时缓冲区。这个函数返回后，
     * dev_venc.cpp 会调用 ss_mpi_venc_release_stream() 归还缓冲区。因此生产代码
     * 必须像 copy_encoded_frame() 那样在回调内深拷贝。这里的 fwrite() 也在
     * 回调返回前完成，所以是安全的。
     */
    void on_stream_come(zero_ipc::util::stream_obj_ptr sobj,
                        zero_ipc::util::stream_head* head,
                        const char* /*audio_data*/,
                        int32_t /*audio_size*/) override
    {
        if (!output_ || !sobj || !head) {
            return;
        }

        // 这个教学文件只保存主码流。子码流即使被启动也会在这里被忽略。
        if (sobj->stream_id() != MAIN_STREAM_ID ||
            head->type != STREAM_NALU_SLICE) {
            return;
        }

        for (uint32_t i = 0;
             i < head->nalu_count && i < MAX_STREAM_NALU_COUNT;
             ++i) {
            const auto& nalu = head->nalu[i];
            if (!nalu.data || nalu.size == 0) {
                continue;
            }

            // VENC 给出的数据已经是 Annex-B H.264，通常以 00 00 00 01 开头，
            // 因此把各个 NALU 顺序写入文件即可得到可播放的裸 H.264 码流。
            // fwrite 可能阻塞，所以这种写法只适合教学。正式程序应当像
            // EncodedStreamChannel 一样尽快复制数据，再交给其他线程处理。
            const std::size_t written =
                std::fwrite(nalu.data, 1, nalu.size, output_);
            if (written != nalu.size) {
                std::fprintf(stderr, "[pipeline_with_chn] write stream failed\n");
                g_stop = 1;
                return;
            }
        }

        const unsigned count = ++frame_count_;
        if (count == 1 || count % 100 == 0) {
            std::printf("[pipeline_with_chn] received %u encoded frames\n", count);
            std::fflush(stdout);
        }
    }

    void on_stream_error(zero_ipc::util::stream_obj_ptr,
                         int32_t error_code) override
    {
        std::fprintf(stderr, "[pipeline_with_chn] stream error=%d\n", error_code);
    }

private:
    const char* output_path_;
    std::FILE* output_{nullptr};
    std::atomic<unsigned> frame_count_{0};
};

} // namespace

int main()
{
    // -------------------------------------------------------------------------
    // 3. 本示例使用项目 rootfs 默认配置对应的参数。
    //
    // Sensor 输出：SC4336P，2560x1440，30 fps，MIPI RAW Bayer
    // 编码输出   ：H.264 CBR，1280x720，15 fps，800 kbps
    // -------------------------------------------------------------------------
    // 传给 sys::init() 的功能开关位。0 表示关闭额外功能；例如 LDC、WDR、
    // AIISP 等功能都是通过不同的 bit 打开，而不是把它当成普通的数值参数。
    constexpr int kSystemFlags = 0;

    // MIPI 高速 lane 的分配模式，最终传给 /dev/ot_mipi_rx。0 是当前板卡的
    // 默认模式；它必须和硬件实际接线、sensor 驱动配置相匹配。
    constexpr int kLaneMode = 0;

    // 传感器输入 RAW 图像的尺寸，不是最终 WebRTC 视频尺寸。
    constexpr int kSensorWidth = 2560;
    constexpr int kSensorHeight = 1440;

    // 传感器出图频率，表示 MIPI/VI/ISP 每秒接收多少张 RAW 图像。
    // 编码器可以在后面把它降到更低的输出帧率。
    constexpr int kSensorFps = 30;

    // WDR（宽动态范围）模式。0 表示关闭；开启时通常需要 sensor、VI pipe
    // 和 ISP 同时配置多帧或多曝光融合，不能只修改这一行。
    constexpr int kWdrMode = 0;

    // 海思 VI/VENC 的逻辑通道编号。当前工程 MAX_CHANNEL == 1，所以只有 0；
    // 双摄像头时通常会为第二路分配 1，但还需要完整扩展设备和资源管理。
    constexpr int kChannelId = 0;

    // 编码输出图像的尺寸。VPSS 会将 ISP 输出的 YUV 按这个尺寸提供给主 VENC。
    constexpr int kEncodeWidth = 1280;
    constexpr int kEncodeHeight = 720;

    // 主码流编码帧率。这里从传感器的 30 fps 降为 15 fps，以降低码率和负载。
    constexpr int kEncodeFps = 15;

    // CBR（恒定码率）目标，单位是 kbps；800 表示约 800 kbit/s，不是 byte/s。
    constexpr int kBitrateKbps = 800;

    // SVC（分层视频编码）开关。0 表示普通 H.264；开启后还需要对应的 VENC
    // SVC 配置，接收端也必须支持相应的分层码流。
    constexpr int kSvcEnabled = 0;
    constexpr const char* kOutputPath = "/tmp/pipeline_with_chn.h264";

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    // -------------------------------------------------------------------------
    // 4. 初始化 MPP 系统，但此时还没有开始出图。
    //
    // chn::init() 内部按顺序完成：
    //
    //   sys::init()
    //     - 配置 RAW/YUV 所需的 VB 视频缓冲池
    //     - ss_mpi_vb_init()
    //     - ss_mpi_sys_init()
    //     - 配置 VI/VPSS 的 online/offline 工作模式
    //
    //   vi_isp::init_hs_mode()
    //     - 通过 /dev/ot_mipi_rx 配置 MIPI lane 模式
    //
    //   venc::init()
    //     - 初始化视频编码模块的公共参数
    // -------------------------------------------------------------------------
    if (!hisilicon::dev::chn::init(
            kSystemFlags,
            static_cast<lane_divide_mode_t>(kLaneMode),
            kSensorWidth,
            kSensorHeight,
            kSensorFps,
            kWdrMode)) {
        std::fprintf(stderr, "[pipeline_with_chn] MPP init failed\n");
        return 1;
    }

    auto pipeline = std::make_shared<EncodedFileSink>(
        "SC4336P", "H264_CBR", kChannelId, kOutputPath);

    if (!pipeline->open_output()) {
        hisilicon::dev::chn::release();
        return 2;
    }

    // -------------------------------------------------------------------------
    // 5. start() 是视频硬件链路真正建立起来的地方。
    //
    // chn::start() 内部可以按以下顺序阅读：
    //
    // A. 根据 "SC4336P" 创建 vi_sc4336p_liner。
    //
    // B. vi_isp::start()：
    //    1) 打开 /dev/ot_mipi_rx，配置 RAW 输入并复位 Sensor；
    //    2) 创建并启动 VI Device、VI Pipe、VI Channel；
    //    3) ss_mpi_isp_init()，再由独立线程调用 ss_mpi_isp_run()；
    //    4) VI Pipe 输入格式是 RGB Bayer RAW；
    //    5) ISP 处理后，VI Channel 输出 YVU semiplanar 420；
    //    6) 创建 VPSS，并用 ss_mpi_sys_bind(VI, VPSS) 连接硬件模块。
    //
    // C. 创建主、子 VENC：
    //    - 主码流的来源是 VPSS；
    //    - ss_mpi_sys_bind(VPSS, VENC) 后，YUV 自动送给编码器；
    //    - ss_mpi_venc_start_chn() 开始接收并编码图像；
    //    - 子码流只 prepare()，有订阅需求时才真正启动。
    //
    // 这里没有对 RAW/YUV 做 get_frame()，是因为模块间采用 bind 模式，数据由
    // MPP/硬件自动流转。只有需要 CPU 分析或保存 YUV 时，才需要主动 get frame。
    // -------------------------------------------------------------------------
    hisilicon::dev::time_osd_options osd;
    osd.enable = false;

    if (!pipeline->start(kEncodeWidth,
                         kEncodeHeight,
                         kEncodeFps,
                         kBitrateKbps,
                         kSvcEnabled,
                         osd)) {
        std::fprintf(stderr, "[pipeline_with_chn] video pipeline start failed\n");
        pipeline->close_output();
        hisilicon::dev::chn::release();
        return 3;
    }

    // -------------------------------------------------------------------------
    // 6. start_capture(true) 启动“读取编码码流”的线程。
    //
    // 名字容易让初学者误会：这里不是开始读取 RAW，而是开始等待 VENC fd。
    // dev_venc.cpp::on_capturing() 会执行：
    //
    //   select(venc_fd)
    //     -> ss_mpi_venc_query_status()
    //     -> ss_mpi_venc_get_stream()
    //     -> process_video_stream()
    //     -> 本类 on_stream_come()
    //     -> ss_mpi_venc_release_stream()
    // -------------------------------------------------------------------------
    // chn 这一层把底层 venc::start_capture() 的 bool 返回值隐藏掉了，
    // 因而这里只负责触发启动，没有可供检查的返回值。
    hisilicon::dev::chn::start_capture(true);

    // 主编码器已经启动，请求一个 IDR，使输出文件尽快从关键帧开始。
    hisilicon::dev::chn::request_i_frame(kChannelId, MAIN_STREAM_ID);

    std::printf("[pipeline_with_chn] pipeline is running\n");
    std::printf("[pipeline_with_chn] H.264 output: %s\n", kOutputPath);
    std::printf("[pipeline_with_chn] press Ctrl+C to stop\n");

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // -------------------------------------------------------------------------
    // 7. 逆序关闭：先停止取编码流，再拆 VENC/VPSS/VI/ISP，最后释放 MPP。
    // -------------------------------------------------------------------------
    hisilicon::dev::chn::start_capture(false);
    pipeline->stop();
    pipeline->close_output();
    pipeline.reset();
    hisilicon::dev::chn::release();

    std::printf("[pipeline_with_chn] stopped; try: ffplay -f h264 %s\n", kOutputPath);
    return 0;
}
