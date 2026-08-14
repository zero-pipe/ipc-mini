/**
 * pipeline_direct_mpp.cpp - 不经过项目设备封装，直接调用 HiSilicon MPP SDK
 *
 * pipeline_with_chn.cpp 使用了项目已有的 dev::chn / vi_isp / venc 封装。
 * 本文件继续向下一层走：所有视频链路动作都在 main() 中直接调用海思 SDK。
 *
 * 数据路径：
 *
 *   SC4336P Sensor
 *       | MIPI RAW Bayer
 *       v
 *   VI Device -> VI Pipe -> ISP -> VI Channel
 *                                      | YVU420
 *                                      v
 *                                    VPSS
 *                                      | 缩放后的 YVU420
 *                                      v
 *                                    VENC
 *                                      | H.264 Annex-B
 *                                      v
 *                           ss_mpi_venc_get_stream()
 *                                      |
 *                                      v
 *                              /tmp/pipeline_direct_mpp.h264
 *
 * 重要说明：
 *
 * 1. 这是面向学习的单 Sensor、单主码流示例，不包含音频、子码流、OSD、
 *    WebRTC、AI、运行时配置和生产级故障恢复。
 * 2. VI -> VPSS -> VENC 使用 bind 模式，RAW/YUV 由硬件内部搬运；CPU 不需要
 *    每帧调用 get_frame()。CPU 只在最后取出编码完成的 H.264。
 * 3. SC4336P 的 MIPI、Bayer、lane 和时钟参数与当前项目板级代码保持一致。
 *    换 Sensor 或换板不能只改宽高，必须同时替换 Sensor 驱动和硬件参数。
 * 4. 本文件不加入 board/Makefile，避免与 app/main.cpp 的 main() 冲突。
 */

// 海思基础类型和缓冲区计算接口。
#include "ot_buffer.h"
#include "ot_common.h"
#include "ot_mipi_rx.h"
#include "ot_sns_ctrl.h"

// 海思 MPP 各模块的直接 API。
#include "ss_mpi_ae.h"
#include "ss_mpi_awb.h"
#include "ss_mpi_isp.h"
#include "ss_mpi_sys.h"
#include "ss_mpi_sys_bind.h"
#include "ss_mpi_vb.h"
#include "ss_mpi_venc.h"
#include "ss_mpi_vi.h"
#include "ss_mpi_vpss.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <thread>
#include <unistd.h>
#include <vector>

// 这个对象由项目中的 SC4336P Sensor 驱动定义：
// devices/hisi_3516cv610/sensor/smart_sc4336p/sc4336p_cmos.c
// 它向 ISP 提供 Sensor 初始化、寄存器更新、AE/AWB 参数等回调。
extern ot_isp_sns_obj g_sns_sc4336p_obj;

namespace {

constexpr const char* kMipiDevice = "/dev/ot_mipi_rx";
constexpr const char* kOutputPath = "/tmp/pipeline_direct_mpp.h264";

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int)
{
    g_stop = 1;
}

/**
 * 海思 API 通常返回 td_s32：TD_SUCCESS 表示成功，其他值是模块错误码。
 * 这个辅助函数只负责统一打印错误，不隐藏任何 Pipeline 操作。
 */
bool mpp_ok(td_s32 result, const char* operation)
{
    if (result == TD_SUCCESS) {
        return true;
    }
    std::fprintf(stderr, "[pipeline_direct_mpp] %s failed: %#x\n", operation, result);
    return false;
}

bool ioctl_ok(int fd, unsigned long request, void* argument,
              const char* operation)
{
    if (::ioctl(fd, request, argument) == 0) {
        return true;
    }
    std::fprintf(stderr, "[pipeline_direct_mpp] %s failed: errno=%d (%s)\n",
                 operation, errno, std::strerror(errno));
    return false;
}

} // namespace

int main()
{
    // =========================================================================
    // A. 固定参数和模块编号
    // =========================================================================

    constexpr td_u32 kSensorWidth = 2560;
    constexpr td_u32 kSensorHeight = 1440;
    constexpr td_s32 kSensorFps = 30;

    constexpr td_u32 kEncodeWidth = 1280;
    constexpr td_u32 kEncodeHeight = 720;
    constexpr td_s32 kEncodeFps = 15;
    constexpr td_u32 kBitrateKbps = 800;

    // 一个 MPP 模块常用“设备号 + 通道号”定位资源。
    // 单摄像头示例全部使用编号 0。
    constexpr ot_vi_dev kViDev = 0;
    constexpr ot_vi_pipe kViPipe = 0;
    constexpr ot_vi_chn kViChn = 0;
    constexpr ot_vpss_grp kVpssGrp = 0;
    constexpr ot_vpss_chn kVpssChn = 0;
    constexpr ot_venc_chn kVencChn = 0;
    constexpr combo_dev_t kMipiDev = 0;
    constexpr sns_clk_source_t kSensorClock = 0;
    constexpr td_s8 kSensorI2cBus = 0;

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    // =========================================================================
    // B. 资源状态
    //
    // 直接调用 SDK 时，没有 C++ 对象替我们析构，所以必须记录哪些模块已启动。
    // cleanup 标签会按照启动的反方向释放资源。
    // =========================================================================

    bool vb_initialized = false;
    bool system_initialized = false;
    bool mipi_started = false;
    bool vi_dev_enabled = false;
    bool vi_bound_to_pipe = false;
    bool vi_pipe_created = false;
    bool vi_pipe_started = false;
    bool vi_chn_enabled = false;
    bool sensor_callback_registered = false;
    bool ae_registered = false;
    bool awb_registered = false;
    bool isp_memory_initialized = false;
    bool isp_initialized = false;
    bool vpss_created = false;
    bool vpss_started = false;
    bool vpss_chn_enabled = false;
    bool vi_bound_to_vpss = false;
    bool venc_created = false;
    bool vpss_bound_to_venc = false;
    bool venc_started = false;
    int exit_code = 0;

    std::thread isp_thread;
    std::FILE* output = nullptr;
    int mipi_fd = -1;

    // =========================================================================
    // C. 准备各模块属性
    //
    // 这里先只“填写配置结构体”，尚未启动任何硬件。
    // 把所有属性集中放在一块，便于对照 RAW -> YUV -> H.264 的格式变化。
    // =========================================================================

    // ----- C1. MIPI：Sensor 在线路上传输 RAW Bayer ---------------------------

    combo_dev_attr_t mipi_attr{};
    mipi_attr.devno = kMipiDev;
    mipi_attr.input_mode = INPUT_MODE_MIPI;
    mipi_attr.data_rate = MIPI_DATA_RATE_X1;
    mipi_attr.img_rect.x = 0;
    mipi_attr.img_rect.y = 0;
    mipi_attr.img_rect.width = kSensorWidth;
    mipi_attr.img_rect.height = kSensorHeight;
    mipi_attr.mipi_attr.input_data_type = DATA_TYPE_RAW_12BIT;
    mipi_attr.mipi_attr.wdr_mode = OT_MIPI_WDR_MODE_NONE;

    // 这是当前开发板上 SC4336P 的物理 lane 接线顺序。
    // -1 表示该 lane 未使用。换板时必须按原理图调整。
    mipi_attr.mipi_attr.lane_id[0] = 2;
    mipi_attr.mipi_attr.lane_id[1] = 0;
    mipi_attr.mipi_attr.lane_id[2] = -1;
    mipi_attr.mipi_attr.lane_id[3] = -1;

    // ----- C2. VI Device：声明输入接口是 MIPI RAW ----------------------------

    ot_vi_dev_attr vi_dev_attr{};
    vi_dev_attr.intf_mode = OT_VI_INTF_MODE_MIPI;
    vi_dev_attr.work_mode = OT_VI_WORK_MODE_MULTIPLEX_1;
    vi_dev_attr.component_mask[0] = 0xfff00000;
    vi_dev_attr.component_mask[1] = 0;
    vi_dev_attr.scan_mode = OT_VI_SCAN_PROGRESSIVE;
    vi_dev_attr.ad_chn_id[0] = -1;
    vi_dev_attr.ad_chn_id[1] = -1;
    vi_dev_attr.ad_chn_id[2] = -1;
    vi_dev_attr.ad_chn_id[3] = -1;
    vi_dev_attr.data_seq = OT_VI_DATA_SEQ_YVYU;
    vi_dev_attr.sync_cfg.vsync = OT_VI_VSYNC_FIELD;
    vi_dev_attr.sync_cfg.vsync_neg = OT_VI_VSYNC_NEG_HIGH;
    vi_dev_attr.sync_cfg.hsync = OT_VI_HSYNC_VALID_SIG;
    vi_dev_attr.sync_cfg.hsync_neg = OT_VI_HSYNC_NEG_HIGH;
    vi_dev_attr.sync_cfg.vsync_valid = OT_VI_VSYNC_VALID_SIG;
    vi_dev_attr.sync_cfg.vsync_valid_neg = OT_VI_VSYNC_VALID_NEG_HIGH;
    vi_dev_attr.data_type = OT_VI_DATA_TYPE_RAW;
    vi_dev_attr.data_reverse = TD_FALSE;
    vi_dev_attr.in_size.width = kSensorWidth;
    vi_dev_attr.in_size.height = kSensorHeight;
    vi_dev_attr.data_rate = OT_DATA_RATE_X1;

    // ----- C3. VI Pipe：这里仍然是 ISP 处理前的 Bayer RAW --------------------

    ot_vi_pipe_attr vi_pipe_attr{};
    vi_pipe_attr.pipe_bypass_mode = OT_VI_PIPE_BYPASS_NONE;
    vi_pipe_attr.isp_bypass = TD_FALSE; // false：RAW 必须经过 ISP。
    vi_pipe_attr.size.width = kSensorWidth;
    vi_pipe_attr.size.height = kSensorHeight;
    vi_pipe_attr.pixel_format = OT_PIXEL_FORMAT_RGB_BAYER_10BPP;
    vi_pipe_attr.compress_mode = OT_COMPRESS_MODE_LINE;
    vi_pipe_attr.frame_rate_ctrl.src_frame_rate = -1;
    vi_pipe_attr.frame_rate_ctrl.dst_frame_rate = -1;

    // ----- C4. ISP：描述 Sensor 的画幅、帧率、Bayer 排列 ----------------------

    ot_isp_pub_attr isp_pub_attr{};
    isp_pub_attr.wnd_rect.x = 0;
    isp_pub_attr.wnd_rect.y = 0;
    isp_pub_attr.wnd_rect.width = kSensorWidth;
    isp_pub_attr.wnd_rect.height = kSensorHeight;
    isp_pub_attr.sns_size.width = kSensorWidth;
    isp_pub_attr.sns_size.height = kSensorHeight;
    isp_pub_attr.frame_rate = kSensorFps;
    isp_pub_attr.bayer_format = OT_ISP_BAYER_BGGR;
    isp_pub_attr.wdr_mode = OT_WDR_MODE_NONE;

    // ----- C5. VI Channel：ISP 之后已经从 RAW 变成 YUV420 --------------------

    ot_vi_chn_attr vi_chn_attr{};
    vi_chn_attr.size.width = kSensorWidth;
    vi_chn_attr.size.height = kSensorHeight;
    vi_chn_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    vi_chn_attr.dynamic_range = OT_DYNAMIC_RANGE_SDR8;
    vi_chn_attr.video_format = OT_VIDEO_FORMAT_LINEAR;
    vi_chn_attr.compress_mode = OT_COMPRESS_MODE_NONE;
    vi_chn_attr.mirror_en = TD_FALSE;
    vi_chn_attr.flip_en = TD_FALSE;
    vi_chn_attr.depth = 0; // bind 模式不需要 CPU 缓存 VI 帧。
    vi_chn_attr.frame_rate_ctrl.src_frame_rate = -1;
    vi_chn_attr.frame_rate_ctrl.dst_frame_rate = -1;

    // ----- C6. VPSS：接收 YUV，完成缩放，再把 YUV 交给编码器 ---------------

    ot_vpss_grp_attr vpss_grp_attr{};
    vpss_grp_attr.max_width = kSensorWidth;
    vpss_grp_attr.max_height = kSensorHeight;
    vpss_grp_attr.dynamic_range = OT_DYNAMIC_RANGE_SDR8;
    vpss_grp_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    vpss_grp_attr.dei_mode = OT_VPSS_DEI_MODE_OFF;
    vpss_grp_attr.frame_rate.src_frame_rate = -1;
    vpss_grp_attr.frame_rate.dst_frame_rate = -1;

    ot_vpss_chn_attr vpss_chn_attr{};
    vpss_chn_attr.width = kEncodeWidth;
    vpss_chn_attr.height = kEncodeHeight;
    vpss_chn_attr.chn_mode = OT_VPSS_CHN_MODE_USER;
    vpss_chn_attr.video_format = OT_VIDEO_FORMAT_LINEAR;
    vpss_chn_attr.dynamic_range = OT_DYNAMIC_RANGE_SDR8;
    vpss_chn_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    vpss_chn_attr.compress_mode = OT_COMPRESS_MODE_SEG_COMPACT;
    vpss_chn_attr.depth = 0;
    vpss_chn_attr.aspect_ratio.mode = OT_ASPECT_RATIO_NONE;
    vpss_chn_attr.frame_rate.src_frame_rate = kSensorFps;
    vpss_chn_attr.frame_rate.dst_frame_rate = kEncodeFps;

    // ----- C7. VENC：把 VPSS 输出的 YUV420 编成 H.264 CBR --------------------

    ot_venc_chn_attr venc_attr{};
    venc_attr.venc_attr.type = OT_PT_H264;
    venc_attr.venc_attr.max_pic_width = kEncodeWidth;
    venc_attr.venc_attr.max_pic_height = kEncodeHeight;
    venc_attr.venc_attr.pic_width = kEncodeWidth;
    venc_attr.venc_attr.pic_height = kEncodeHeight;
    venc_attr.venc_attr.buf_size = std::min<td_u32>(
        kEncodeWidth * kEncodeHeight * 3 / 4, 2 * 1024 * 1024);
    venc_attr.venc_attr.is_by_frame = TD_TRUE;
    venc_attr.venc_attr.profile = 0; // H.264 Baseline profile。
    venc_attr.venc_attr.h264_attr.rcn_ref_share_buf_en = TD_TRUE;
    venc_attr.venc_attr.h264_attr.frame_buf_ratio = 75;

    venc_attr.gop_attr.gop_mode = OT_VENC_GOP_MODE_NORMAL_P;
    venc_attr.gop_attr.normal_p.ip_qp_delta = 3;

    venc_attr.rc_attr.rc_mode = OT_VENC_RC_MODE_H264_CBR;
    venc_attr.rc_attr.h264_cbr.gop = kEncodeFps; // 每秒一个关键帧。
    venc_attr.rc_attr.h264_cbr.stats_time = 1;
    venc_attr.rc_attr.h264_cbr.src_frame_rate = kSensorFps;
    venc_attr.rc_attr.h264_cbr.dst_frame_rate = kEncodeFps;
    venc_attr.rc_attr.h264_cbr.bit_rate = kBitrateKbps;

    // ----- C8. ISP 的 AE/AWB 算法描述 ---------------------------------------

    ot_isp_3a_alg_lib ae_lib{};
    ae_lib.id = kViPipe;
    std::snprintf(ae_lib.lib_name, sizeof(ae_lib.lib_name), "%s",
                  OT_AE_LIB_NAME);

    ot_isp_3a_alg_lib awb_lib{};
    awb_lib.id = kViPipe;
    std::snprintf(awb_lib.lib_name, sizeof(awb_lib.lib_name), "%s",
                  OT_AWB_LIB_NAME);

    // 模块之间的连线也使用统一的 ot_mpp_chn 描述。
    ot_mpp_chn vi_output{};
    vi_output.mod_id = OT_ID_VI;
    vi_output.dev_id = kViPipe;
    vi_output.chn_id = kViChn;

    ot_mpp_chn vpss_input{};
    vpss_input.mod_id = OT_ID_VPSS;
    vpss_input.dev_id = kVpssGrp;
    vpss_input.chn_id = kVpssChn;

    ot_mpp_chn vpss_output = vpss_input;

    ot_mpp_chn venc_input{};
    venc_input.mod_id = OT_ID_VENC;
    venc_input.dev_id = 0;
    venc_input.chn_id = kVencChn;

    // 下列结构会在后面的启动阶段使用。统一在第一次 goto cleanup 之前声明，
    // 避免 C++ 控制流跳过局部对象初始化。
    lane_divide_mode_t lane_mode = static_cast<lane_divide_mode_t>(0);
    combo_dev_t mipi_dev = kMipiDev;
    sns_clk_source_t sensor_clock = kSensorClock;

    ot_vi_wdr_fusion_grp_attr fusion_attr{};
    fusion_attr.wdr_mode = OT_WDR_MODE_NONE;
    fusion_attr.cache_line = kSensorHeight;
    fusion_attr.pipe_id[0] = kViPipe;

    ot_isp_sns_commbus sensor_bus{};
    sensor_bus.i2c_dev = kSensorI2cBus;

    ot_venc_start_param venc_start{};
    venc_start.recv_pic_num = -1; // -1 表示持续编码，直到显式 stop。

    // =========================================================================
    // D. 初始化 VB 和 MPP System
    //
    // VB（Video Buffer）是海思各硬件模块共享的视频内存池。MPP 模块启动前
    // 必须先规划内存。这里使用和正式工程相同的 online + wrap 思路，只申请
    // 一块 VPSS/VENC wrap 缓冲，避免保存完整 RAW/YUV 帧造成大量内存占用。
    // =========================================================================

    // 清理上一次异常退出可能遗留的用户态 MPP 状态。
    ::ss_mpi_sys_exit();
    ::ss_mpi_vb_exit();

    ot_vi_vpss_mode vi_vpss_mode{};
    vi_vpss_mode.mode[0] = OT_VI_ONLINE_VPSS_ONLINE;
    for (int i = 1; i < OT_VI_MAX_PIPE_NUM; ++i) {
        vi_vpss_mode.mode[i] = OT_VI_OFFLINE_VPSS_OFFLINE;
    }

    ot_pic_buf_attr wrap_pic_attr{};
    wrap_pic_attr.width = kEncodeWidth;
    wrap_pic_attr.height = kEncodeHeight;
    wrap_pic_attr.align = OT_DEFAULT_ALIGN;
    wrap_pic_attr.bit_width = OT_DATA_BIT_WIDTH_8;
    wrap_pic_attr.pixel_format = OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    wrap_pic_attr.compress_mode = OT_COMPRESS_MODE_SEG_COMPACT;
    wrap_pic_attr.video_format = OT_VIDEO_FORMAT_LINEAR;

    constexpr td_u32 kWrapLines = 256;
    const td_u32 wrap_buffer_size =
        ::ot_comm_get_vpss_venc_wrap_buf_size(&wrap_pic_attr, kWrapLines);

    ot_vpss_chn_buf_wrap_attr wrap_attr{};
    wrap_attr.enable = TD_TRUE;
    wrap_attr.buf_line = kWrapLines;
    wrap_attr.buf_size = wrap_buffer_size;

    ot_vb_cfg vb_cfg{};
    vb_cfg.max_pool_cnt = 1;
    vb_cfg.common_pool[0].blk_size = wrap_buffer_size;
    vb_cfg.common_pool[0].blk_cnt = 1;

    if (!mpp_ok(::ss_mpi_vb_set_cfg(&vb_cfg), "ss_mpi_vb_set_cfg") ||
        !mpp_ok(::ss_mpi_vb_init(), "ss_mpi_vb_init")) {
        exit_code = 1;
        goto cleanup;
    }
    vb_initialized = true;

    if (!mpp_ok(::ss_mpi_sys_init(), "ss_mpi_sys_init")) {
        exit_code = 1;
        goto cleanup;
    }
    system_initialized = true;

    if (!mpp_ok(::ss_mpi_sys_set_vi_vpss_mode(&vi_vpss_mode),
                "ss_mpi_sys_set_vi_vpss_mode")) {
        exit_code = 1;
        goto cleanup;
    }
    if (!mpp_ok(::ss_mpi_sys_set_vi_aiisp_mode(
                    kViPipe, OT_VI_AIISP_MODE_DEFAULT),
                "ss_mpi_sys_set_vi_aiisp_mode")) {
        exit_code = 1;
        goto cleanup;
    }

    // =========================================================================
    // E. 直接配置 MIPI RX 和 Sensor 时钟
    //
    // 这一段还不是读 RAW；它是在告诉 MIPI 接收器“线路上来的 RAW 是什么”。
    // =========================================================================

    mipi_fd = ::open(kMipiDevice, O_RDWR);
    if (mipi_fd < 0) {
        std::fprintf(stderr, "[pipeline_direct_mpp] open %s failed: %s\n",
                     kMipiDevice, std::strerror(errno));
        exit_code = 2;
        goto cleanup;
    }

    // 从这里开始，只要后续任一 ioctl 失败，cleanup 都会尝试复位 MIPI。
    mipi_started = true;
    if (!ioctl_ok(mipi_fd, OT_MIPI_SET_HS_MODE, &lane_mode,
                  "OT_MIPI_SET_HS_MODE") ||
        !ioctl_ok(mipi_fd, OT_MIPI_ENABLE_MIPI_CLOCK,
                  &mipi_dev,
                  "OT_MIPI_ENABLE_MIPI_CLOCK") ||
        !ioctl_ok(mipi_fd, OT_MIPI_RESET_MIPI,
                  &mipi_dev,
                  "OT_MIPI_RESET_MIPI") ||
        !ioctl_ok(mipi_fd, OT_MIPI_SET_DEV_ATTR, &mipi_attr,
                  "OT_MIPI_SET_DEV_ATTR") ||
        !ioctl_ok(mipi_fd, OT_MIPI_UNRESET_MIPI,
                  &mipi_dev,
                  "OT_MIPI_UNRESET_MIPI") ||
        !ioctl_ok(mipi_fd, OT_MIPI_ENABLE_SENSOR_CLOCK,
                  &sensor_clock,
                  "OT_MIPI_ENABLE_SENSOR_CLOCK") ||
        !ioctl_ok(mipi_fd, OT_MIPI_RESET_SENSOR,
                  &sensor_clock,
                  "OT_MIPI_RESET_SENSOR") ||
        !ioctl_ok(mipi_fd, OT_MIPI_UNRESET_SENSOR,
                  &sensor_clock,
                  "OT_MIPI_UNRESET_SENSOR")) {
        exit_code = 2;
        goto cleanup;
    }
    ::close(mipi_fd);
    mipi_fd = -1;

    // =========================================================================
    // F. 启动 VI：让 MIPI RAW 进入 VI Pipe
    // =========================================================================

    if (!mpp_ok(::ss_mpi_vi_set_dev_attr(kViDev, &vi_dev_attr),
                "ss_mpi_vi_set_dev_attr") ||
        !mpp_ok(::ss_mpi_vi_enable_dev(kViDev),
                "ss_mpi_vi_enable_dev")) {
        exit_code = 3;
        goto cleanup;
    }
    vi_dev_enabled = true;

    if (!mpp_ok(::ss_mpi_vi_bind(kViDev, kViPipe), "ss_mpi_vi_bind")) {
        exit_code = 3;
        goto cleanup;
    }
    vi_bound_to_pipe = true;

    if (!mpp_ok(::ss_mpi_vi_set_wdr_fusion_grp_attr(0, &fusion_attr),
                "ss_mpi_vi_set_wdr_fusion_grp_attr") ||
        !mpp_ok(::ss_mpi_vi_create_pipe(kViPipe, &vi_pipe_attr),
                "ss_mpi_vi_create_pipe")) {
        exit_code = 3;
        goto cleanup;
    }
    vi_pipe_created = true;

    if (!mpp_ok(::ss_mpi_vi_start_pipe(kViPipe),
                "ss_mpi_vi_start_pipe")) {
        exit_code = 3;
        goto cleanup;
    }
    vi_pipe_started = true;

    if (!mpp_ok(::ss_mpi_vi_set_chn_attr(kViPipe, kViChn, &vi_chn_attr),
                "ss_mpi_vi_set_chn_attr") ||
        !mpp_ok(::ss_mpi_vi_enable_chn(kViPipe, kViChn),
                "ss_mpi_vi_enable_chn")) {
        exit_code = 3;
        goto cleanup;
    }
    vi_chn_enabled = true;

    // =========================================================================
    // G. 启动 Sensor 回调和 ISP
    //
    // ISP 不是普通“一次调用就处理完”的函数。初始化后需要一个线程长期执行
    // ss_mpi_isp_run()，驱动 AE、AWB 和每帧 ISP 调度。
    // =========================================================================

    if (!mpp_ok(g_sns_sc4336p_obj.pfn_register_callback(
                    kViPipe, &ae_lib, &awb_lib),
                "sensor pfn_register_callback")) {
        exit_code = 4;
        goto cleanup;
    }
    sensor_callback_registered = true;

    if (!mpp_ok(g_sns_sc4336p_obj.pfn_set_bus_info(kViPipe, sensor_bus),
                "sensor pfn_set_bus_info") ||
        !mpp_ok(::ss_mpi_ae_register(kViPipe, &ae_lib),
                "ss_mpi_ae_register")) {
        exit_code = 4;
        goto cleanup;
    }
    ae_registered = true;

    if (!mpp_ok(::ss_mpi_awb_register(kViPipe, &awb_lib),
                "ss_mpi_awb_register")) {
        exit_code = 4;
        goto cleanup;
    }
    awb_registered = true;

    if (!mpp_ok(::ss_mpi_isp_mem_init(kViPipe),
                "ss_mpi_isp_mem_init")) {
        exit_code = 4;
        goto cleanup;
    }
    isp_memory_initialized = true;

    if (!mpp_ok(::ss_mpi_isp_set_pub_attr(kViPipe, &isp_pub_attr),
                "ss_mpi_isp_set_pub_attr") ||
        !mpp_ok(::ss_mpi_isp_init(kViPipe), "ss_mpi_isp_init")) {
        exit_code = 4;
        goto cleanup;
    }
    isp_initialized = true;

    isp_thread = std::thread([] {
        const td_s32 result = ::ss_mpi_isp_run(kViPipe);
        if (result != TD_SUCCESS && !g_stop) {
            std::fprintf(stderr, "[pipeline_direct_mpp] ss_mpi_isp_run exited: %#x\n",
                         result);
            g_stop = 1;
        }
    });

    // =========================================================================
    // H. 启动 VPSS：接收 ISP 输出的 YUV，并缩放成编码尺寸
    // =========================================================================

    if (!mpp_ok(::ss_mpi_vpss_create_grp(kVpssGrp, &vpss_grp_attr),
                "ss_mpi_vpss_create_grp")) {
        exit_code = 5;
        goto cleanup;
    }
    vpss_created = true;

    if (!mpp_ok(::ss_mpi_vpss_start_grp(kVpssGrp),
                "ss_mpi_vpss_start_grp")) {
        exit_code = 5;
        goto cleanup;
    }
    vpss_started = true;

    if (!mpp_ok(::ss_mpi_vpss_set_chn_attr(
                    kVpssGrp, kVpssChn, &vpss_chn_attr),
                "ss_mpi_vpss_set_chn_attr")) {
        exit_code = 5;
        goto cleanup;
    }

    if (!mpp_ok(::ss_mpi_vpss_set_chn_buf_wrap(
                    kVpssGrp, kVpssChn, &wrap_attr),
                "ss_mpi_vpss_set_chn_buf_wrap") ||
        !mpp_ok(::ss_mpi_vpss_enable_chn(kVpssGrp, kVpssChn),
                "ss_mpi_vpss_enable_chn")) {
        exit_code = 5;
        goto cleanup;
    }
    vpss_chn_enabled = true;

    // 这条 bind 建立后，VI Channel 的 YUV 会由硬件自动送进 VPSS。
    if (!mpp_ok(::ss_mpi_sys_bind(&vi_output, &vpss_input),
                "ss_mpi_sys_bind(VI -> VPSS)")) {
        exit_code = 5;
        goto cleanup;
    }
    vi_bound_to_vpss = true;

    // =========================================================================
    // I. 启动 VENC：把 VPSS 的 YUV 编码为 H.264
    // =========================================================================

    if (!mpp_ok(::ss_mpi_venc_create_chn(kVencChn, &venc_attr),
                "ss_mpi_venc_create_chn")) {
        exit_code = 6;
        goto cleanup;
    }
    venc_created = true;

    // 这条 bind 建立后，VPSS 输出的 YUV 会由硬件自动送进 VENC。
    if (!mpp_ok(::ss_mpi_sys_bind(&vpss_output, &venc_input),
                "ss_mpi_sys_bind(VPSS -> VENC)")) {
        exit_code = 6;
        goto cleanup;
    }
    vpss_bound_to_venc = true;

    if (!mpp_ok(::ss_mpi_venc_start_chn(kVencChn, &venc_start),
                "ss_mpi_venc_start_chn")) {
        exit_code = 6;
        goto cleanup;
    }
    venc_started = true;

    // 请求 IDR，让文件尽快从关键帧开始，方便播放器直接解码。
    ::ss_mpi_venc_request_idr(kVencChn, TD_TRUE);

    // =========================================================================
    // J. 应用层取出编码码流
    //
    // 到这里 RAW -> ISP -> YUV -> H.264 都已经由硬件完成。
    // 应用只等待 VENC fd，然后拿出 H.264 pack 写文件。
    // =========================================================================

    output = std::fopen(kOutputPath, "wb");
    if (!output) {
        std::perror("[pipeline_direct_mpp] fopen");
        exit_code = 7;
        goto cleanup;
    }

    {
        const int venc_fd = ::ss_mpi_venc_get_fd(kVencChn);
        if (venc_fd < 0) {
            std::fprintf(stderr, "[pipeline_direct_mpp] ss_mpi_venc_get_fd failed\n");
            exit_code = 7;
            goto cleanup;
        }

        unsigned frame_count = 0;
        std::printf("[pipeline_direct_mpp] direct SDK pipeline is running\n");
        std::printf("[pipeline_direct_mpp] output: %s\n", kOutputPath);
        std::printf("[pipeline_direct_mpp] press Ctrl+C to stop\n");

        while (!g_stop) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(venc_fd, &read_fds);

            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 200000;

            const int selected =
                ::select(venc_fd + 1, &read_fds, nullptr, nullptr, &timeout);
            if (selected < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::perror("[pipeline_direct_mpp] select");
                exit_code = 7;
                break;
            }
            if (selected == 0 || !FD_ISSET(venc_fd, &read_fds)) {
                continue;
            }

            // 先查询这一帧包含多少个 pack，再准备对应大小的数组。
            ot_venc_chn_status status{};
            if (!mpp_ok(::ss_mpi_venc_query_status(kVencChn, &status),
                        "ss_mpi_venc_query_status")) {
                continue;
            }
            if (status.cur_packs == 0) {
                continue;
            }

            std::vector<ot_venc_pack> packs(status.cur_packs);
            ot_venc_stream stream{};
            stream.pack = packs.data();
            stream.pack_cnt = status.cur_packs;

            if (!mpp_ok(::ss_mpi_venc_get_stream(
                            kVencChn, &stream, TD_TRUE),
                        "ss_mpi_venc_get_stream")) {
                continue;
            }

            bool write_ok = true;
            for (td_u32 i = 0; i < stream.pack_cnt; ++i) {
                const ot_venc_pack& pack = stream.pack[i];
                if (!pack.addr || pack.offset > pack.len) {
                    continue;
                }

                const td_u8* data = pack.addr + pack.offset;
                const std::size_t size = pack.len - pack.offset;
                if (std::fwrite(data, 1, size, output) != size) {
                    write_ok = false;
                    break;
                }
            }

            // get_stream() 得到的是 VENC 管理的缓冲区。无论 fwrite 是否成功，
            // 都必须 release，否则编码器的码流缓冲会逐渐耗尽并停止出流。
            ::ss_mpi_venc_release_stream(kVencChn, &stream);

            if (!write_ok) {
                std::fprintf(stderr, "[pipeline_direct_mpp] write output failed\n");
                exit_code = 7;
                break;
            }

            ++frame_count;
            if (frame_count == 1 || frame_count % 100 == 0) {
                std::printf("[pipeline_direct_mpp] received %u encoded frames\n",
                            frame_count);
                std::fflush(stdout);
            }
        }
    }

cleanup:
    // =========================================================================
    // K. 按启动的反方向释放资源
    //
    // 正向：System -> MIPI -> VI -> ISP -> VPSS -> VENC
    // 反向：VENC -> VPSS -> ISP -> VI -> MIPI -> System
    // =========================================================================

    if (output) {
        std::fclose(output);
        output = nullptr;
    }

    if (venc_started) {
        ::ss_mpi_venc_stop_chn(kVencChn);
    }
    if (vpss_bound_to_venc) {
        ::ss_mpi_sys_unbind(&vpss_output, &venc_input);
    }
    if (venc_created) {
        ::ss_mpi_venc_destroy_chn(kVencChn);
    }

    if (vi_bound_to_vpss) {
        ::ss_mpi_sys_unbind(&vi_output, &vpss_input);
    }
    if (vpss_chn_enabled) {
        ::ss_mpi_vpss_disable_chn(kVpssGrp, kVpssChn);
    }
    if (vpss_started) {
        ::ss_mpi_vpss_stop_grp(kVpssGrp);
    }
    if (vpss_created) {
        ::ss_mpi_vpss_destroy_grp(kVpssGrp);
    }

    if (isp_initialized || isp_memory_initialized) {
        // ss_mpi_isp_exit() 会使另一个线程中的 ss_mpi_isp_run() 返回。
        ::ss_mpi_isp_exit(kViPipe);
    }
    if (isp_thread.joinable()) {
        isp_thread.join();
    }
    if (awb_registered) {
        ::ss_mpi_awb_unregister(kViPipe, &awb_lib);
    }
    if (ae_registered) {
        ::ss_mpi_ae_unregister(kViPipe, &ae_lib);
    }
    if (sensor_callback_registered) {
        g_sns_sc4336p_obj.pfn_un_register_callback(
            kViPipe, &ae_lib, &awb_lib);
    }

    if (vi_chn_enabled) {
        ::ss_mpi_vi_disable_chn(kViPipe, kViChn);
    }
    if (vi_pipe_started) {
        ::ss_mpi_vi_stop_pipe(kViPipe);
    }
    if (vi_pipe_created) {
        ::ss_mpi_vi_destroy_pipe(kViPipe);
    }
    if (vi_bound_to_pipe) {
        ::ss_mpi_vi_unbind(kViDev, kViPipe);
    }
    if (vi_dev_enabled) {
        ::ss_mpi_vi_disable_dev(kViDev);
    }

    if (mipi_fd >= 0) {
        ::close(mipi_fd);
        mipi_fd = -1;
    }
    if (mipi_started) {
        const int fd = ::open(kMipiDevice, O_RDWR);
        if (fd >= 0) {
            ::ioctl(fd, OT_MIPI_RESET_SENSOR, &sensor_clock);
            ::ioctl(fd, OT_MIPI_RESET_MIPI, &mipi_dev);
            ::ioctl(fd, OT_MIPI_DISABLE_MIPI_CLOCK, &mipi_dev);
            ::close(fd);
        }
    }

    if (system_initialized) {
        ::ss_mpi_sys_exit();
    }
    if (vb_initialized) {
        ::ss_mpi_vb_exit();
    }

    if (exit_code == 0) {
        std::printf("[pipeline_direct_mpp] stopped; try: ffplay -f h264 %s\n",
                    kOutputPath);
    }
    return exit_code;
}
