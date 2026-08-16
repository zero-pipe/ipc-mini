# 海思视频 Pipeline 学习笔记

本文整理本次对 `ipc_mini` 视频链路的学习与分析，重点说明：

- `channel_id`、`stream_id` 分别表示什么；
- 海思的 `VI Device`、`VI Pipe`、`VI Channel` 为什么要分层；
- Sensor RAW 数据在哪里进入系统；
- ISP 在哪里启动，YUV 在哪里产生；
- VPSS 和 VENC 分别做什么；
- 编码数据何时被应用读取；
- 编码数据如何进入 `MediaSource`；
- `demo_1.cpp` 和 `demo_2.cpp` 应该怎样阅读。

本文不讨论 C++ 所有权、命名空间等通用编程话题。

---

## 1. 先看完整数据链路

当前项目的视频主链路可以概括为：

```text
SC4336P Sensor
    |
    | MIPI RAW Bayer
    v
VI Device
    |
    v
VI Pipe
    |
    | ISP 处理 RAW
    v
VI Channel
    |
    | YVU420
    v
VPSS
    |
    | 缩放/帧率处理后的 YVU420
    v
VENC
    |
    | H.264/H.265 NALU
    v
应用取流线程
    |
    v
EncodedStreamChannel
    |
    | 深拷贝为 MediaFrame
    v
MediaSource
    |                    |
    | GOP Cache          | Subscribers
    v                    v
历史关键帧缓存          WebRTC 等协议插件
```

最重要的认识是：

> 应用程序不会逐帧读取 RAW，再用 CPU 把 RAW 转换成 YUV，也不会用 CPU 把 YUV 编码成 H.264。

海思芯片内部的 VI、ISP、VPSS、VENC 硬件完成这些工作。应用程序主要负责：

1. 配置各硬件模块；
2. 启动各硬件模块；
3. 使用 `ss_mpi_sys_bind()` 建立模块之间的数据连接；
4. 从 VENC 获取最终编码码流；
5. 将编码码流交给业务层。

---

## 2. 基础名词

### 2.1 RAW

Sensor 感光后产生的原始 Bayer 数据，还不是普通播放器能显示的彩色图像。

当前 SC4336P 配置中可以看到：

```cpp
m_vi_pipe_attr.pixel_format = OT_PIXEL_FORMAT_RGB_BAYER_10BPP;
```

这表示 VI Pipe 中的数据仍是 10 bit Bayer RAW。

RAW 通常还需要经过：

- 去马赛克；
- 自动曝光 AE；
- 自动白平衡 AWB；
- 黑电平校正；
- 降噪；
- Gamma；
- 色彩校正；
- 锐化；
- WDR/去雾等处理。

这些主要由 ISP 完成。

### 2.2 ISP

ISP 是 Image Signal Processor，即图像信号处理器。

它负责把 Sensor 的 Bayer RAW 加工成可继续使用的图像。ISP 并不是一个普通的 C++ 图像处理函数，而是芯片中的专用硬件和对应的运行框架。

项目中 ISP 的核心启动代码是：

```cpp
ss_mpi_isp_mem_init(vi_pipe);
ss_mpi_isp_set_pub_attr(vi_pipe, &m_isp_pub_attr);
ss_mpi_isp_init(vi_pipe);
```

随后需要一个长期运行的线程调用：

```cpp
ss_mpi_isp_run(vi_pipe);
```

这个线程驱动每帧 ISP 处理，以及 AE、AWB 等算法调度。

### 2.3 YUV

ISP 处理之后，VI Channel 输出的已经不是 RAW，而是 YUV。

当前配置使用：

```cpp
OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420
```

它是 YUV420 半平面格式。这里写的是 `YVU`，说明色度平面的排列顺序是 V、U，而不是 U、V。

项目中的格式变化可以简单理解为：

```text
VI Pipe 输入：RGB Bayer RAW 10bit
                   |
                   | ISP
                   v
VI Channel 输出：YVU420 8bit
```

### 2.4 VPSS

VPSS 是 Video Process Sub-System，即视频处理子系统。

它位于 ISP/VI 和编码器之间，常用来完成：

- 缩放；
- 裁剪；
- 帧率转换；
- 镜像/翻转；
- 多路不同尺寸输出；
- 一些前处理能力。

本项目中 Sensor 可以输出 `2560x1440`，而编码输出可能是 `1280x720`。这两个尺寸不同，VPSS 就处于中间处理位置。

### 2.5 VENC

VENC 是 Video Encoder，即视频编码器。

它接收 YUV 图像，输出压缩码流：

```text
YUV420 -> H.264/H.265
```

它负责：

- H.264/H.265 编码；
- CBR/AVBR 码率控制；
- GOP；
- I 帧、P 帧；
- SPS/PPS；
- IDR 关键帧；
- SVC 等编码功能。

### 2.6 MPP

MPP 可以理解为海思提供的媒体处理平台。`ss_mpi_*` API 用于控制 VI、ISP、VPSS、VENC、VB 等模块。

MPP 模块通常不是由应用逐帧搬运数据，而是通过 `bind` 形成硬件数据流。

### 2.7 VB

VB 是 Video Buffer，即视频缓冲池。

RAW 和 YUV 一帧可能占用大量内存。海思模块启动之前，需要先规划共享视频内存：

```cpp
ss_mpi_vb_set_cfg(&vb_cfg);
ss_mpi_vb_init();
ss_mpi_sys_init();
```

VB 配置不足时，VI、VPSS 或 VENC 可能因为拿不到视频缓冲而无法工作。

---

## 3. VI Device、VI Pipe、VI Channel

VI 是 Video Input，即视频输入。

最容易记忆的方式是：

```text
VI Device  = 从哪里接收
VI Pipe    = 如何处理这一股 RAW
VI Channel = 从哪里输出处理后的图像
```

三者组成：

```text
Sensor
  |
  | MIPI RAW
  v
VI Device 0        物理输入入口
  |
  v
VI Pipe 0          RAW/ISP 处理流水线
  |
  v
VI Channel 0       处理后图像的输出口
  |
  | YUV
  v
VPSS
```

### 3.1 VI Device：物理输入入口

`VI Device` 描述摄像头数据怎样进入芯片，主要关心：

- 输入接口是 MIPI、BT.656 还是其他接口；
- 输入尺寸；
- 输入数据是 RAW 还是 YUV；
- 扫描方式；
- 数据速率；
- 物理接收属性。

典型 API：

```cpp
ss_mpi_vi_set_dev_attr(vi_dev, &vi_dev_attr);
ss_mpi_vi_enable_dev(vi_dev);
```

它可以理解成摄像头数据进入芯片的物理大门。

### 3.2 VI Pipe：RAW/ISP 处理流水线

这里的 `Pipe` 是图像处理流水线，不是 Linux 进程间通信的管道。

VI Pipe 接收 Device 送来的 RAW 数据，并关联 ISP 处理。它主要描述：

- RAW Bayer 格式；
- RAW 位宽；
- 图像尺寸；
- 是否绕过 ISP；
- WDR 多曝光处理；
- 使用哪一路 Sensor/ISP 参数。

典型 API：

```cpp
ss_mpi_vi_bind(vi_dev, vi_pipe);
ss_mpi_vi_create_pipe(vi_pipe, &vi_pipe_attr);
ss_mpi_vi_start_pipe(vi_pipe);
```

当前项目配置：

```cpp
vi_pipe_attr.pixel_format = OT_PIXEL_FORMAT_RGB_BAYER_10BPP;
vi_pipe_attr.isp_bypass = TD_FALSE;
```

含义是：

- Pipe 中仍然是 Bayer RAW；
- 不绕过 ISP，RAW 必须经过 ISP 处理。

为什么 Device 和 Pipe 不能合并？

因为一个物理输入可能需要多条处理流水线。例如 WDR 场景：

```text
VI Device
  |-- VI Pipe 0：长曝光 RAW
  `-- VI Pipe 1：短曝光 RAW
              |
              v
           ISP 融合
```

所以 Device 表达物理输入，Pipe 表达处理路径。

### 3.3 VI Channel：处理后图像的输出口

VI Channel 是 Pipe/ISP 处理后的输出端口。

典型 API：

```cpp
ss_mpi_vi_set_chn_attr(vi_pipe, vi_chn, &vi_chn_attr);
ss_mpi_vi_enable_chn(vi_pipe, vi_chn);
```

当前项目中 Channel 配置为：

```cpp
vi_chn_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
```

这说明 VI Channel 输出的已经是 YUV420，而不是 Bayer RAW。

Channel 还可以描述：

- 输出尺寸；
- 输出像素格式；
- 镜像和翻转；
- 压缩方式；
- 输出帧率；
- CPU 是否需要缓存帧。

当前项目把 VI Channel 直接绑定给 VPSS：

```cpp
ss_mpi_sys_bind(&vi_output, &vpss_input);
```

因此 CPU 不需要读取 VI Channel 的 YUV 帧。

### 3.4 为什么三个编号都是 0

当前单摄像头代码中常见：

```cpp
kViDev = 0;
kViPipe = 0;
kViChn = 0;
```

虽然值都是 `0`，它们属于不同编号空间，不是同一个对象：

```text
VI Device 0
    `-- VI Pipe 0
          `-- VI Channel 0
```

类似地，项目还有：

- `VPSS Group 0`；
- `VPSS Channel 0`；
- `VENC Channel 0`。

这些编号都属于各自的硬件模块。

---

## 4. channel_id 和 stream_id

项目上层还有两个业务概念：

```text
channel_id：第几路摄像头/采集编码 Pipeline
stream_id ：这路摄像头下面的哪一路码流
```

可以理解为：

```text
channel 0：摄像头 0
  |-- stream 0：主码流
  |-- stream 1：子码流
  `-- stream 2：AI 流
```

当前项目定义：

```cpp
#define MAIN_STREAM_ID 0
#define SUB_STREAM_ID  1
#define AI_STREAM_ID   2
```

对应关系：

| stream_id | 含义 |
|---|---|
| `0` | 主码流，主要分辨率和码率 |
| `1` | 子码流，当前为 `720x480` |
| `2` | AI/YOLO 视频流 |

### 4.1 channel_id 不是音视频类型

`channel_id` 不表示“视频通道还是音频通道”。

音频和视频由媒体类型区分：

```cpp
MediaType::Video
MediaType::Audio
```

在 `MediaSource` 中，同一个 `stream_id` 可以同时具有视频 Track 和音频 Track。

### 4.2 双摄像头时怎样理解

从架构概念上，双摄可以组织成：

```text
channel 0：摄像头 0
  |-- stream 0：主码流
  `-- stream 1：子码流

channel 1：摄像头 1
  |-- stream 0：主码流
  `-- stream 1：子码流
```

但当前项目写有：

```cpp
#define MAX_CHANNEL 1
```

因此当前实际上只支持 `channel_id == 0`。要支持双摄，不能只把编号改成 `1`，还需要扩展：

- MIPI/VI Device 配置；
- VI Pipe 和 Channel；
- VPSS/VENC 资源；
- VB 内存规划；
- Sensor 驱动实例；
- `Application` 和 `MediaSource` 的组织方式；
- 配置文件和协议层的摄像头选择。

### 4.3 不要混淆两类 Channel

下面这些名字相似，但不是同一个概念：

| 名称 | 含义 |
|---|---|
| `VI Channel` | 海思 VI 模块的图像输出端口 |
| `VPSS Channel` | VPSS 的一路处理输出 |
| `VENC Channel` | 一个硬件编码器实例 |
| 项目的 `channel_id` | 一路逻辑摄像头/媒体 Pipeline |

---

## 5. Sensor RAW 在哪里进入

MIPI 接收器配置位于 `dev_vi_isp.cpp`，设备节点是：

```text
/dev/ot_mipi_rx
```

程序通过 `open()` 和 `ioctl()` 完成：

```cpp
OT_MIPI_SET_HS_MODE
OT_MIPI_ENABLE_MIPI_CLOCK
OT_MIPI_RESET_MIPI
OT_MIPI_SET_DEV_ATTR
OT_MIPI_UNRESET_MIPI
OT_MIPI_ENABLE_SENSOR_CLOCK
OT_MIPI_RESET_SENSOR
OT_MIPI_UNRESET_SENSOR
```

这些操作完成的是：

- MIPI lane 配置；
- MIPI 时钟启动；
- MIPI 接收器复位；
- RAW 数据格式配置；
- Sensor 时钟启动；
- Sensor 复位和启动。

这里看不到：

```cpp
read(sensor_fd, raw_buffer, raw_size);
```

原因是 RAW 数据通过 MIPI RX 和 VI 硬件自动进入 VI Pipe，并不是由应用逐帧读取。

如果业务确实需要保存 RAW，通常需要使用海思专门的 VI Pipe frame/dump 接口，而不是普通的文件 `read()`。

---

## 6. Sensor 驱动与 ISP 的关系

以 SC4336P 为例，Sensor 相关代码位于：

```text
devices/hisi_3516cv610/sensor/smart_sc4336p/
```

主要文件包括：

```text
sc4336p_cmos.c
sc4336p_cmos_param.h
sc4336p_sensor_ctrl.c
```

Sensor 驱动向 ISP 提供一个 `ot_isp_sns_obj`：

```cpp
g_sns_sc4336p_obj
```

其中包含：

- 注册/注销 ISP 回调；
- 设置 I2C 总线；
- Sensor 初始化；
- Sensor 寄存器更新；
- 曝光和增益控制；
- 图像模式切换等。

启动 ISP 前会执行：

```cpp
g_sns_sc4336p_obj.pfn_register_callback(...);
g_sns_sc4336p_obj.pfn_set_bus_info(...);
ss_mpi_ae_register(...);
ss_mpi_awb_register(...);
```

因此 ISP 不只是处理像素，它还会通过 Sensor 回调动态调整曝光、模拟增益等 Sensor 参数。

---

## 7. YUV 如何进入 VPSS

VI Channel 输出格式配置为 YUV420：

```cpp
vi_chn_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
```

VPSS 输入 Group 也配置为 YUV420：

```cpp
vpss_grp_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
```

然后建立连接：

```cpp
ot_mpp_chn vi_output{};
vi_output.mod_id = OT_ID_VI;

ot_mpp_chn vpss_input{};
vpss_input.mod_id = OT_ID_VPSS;

ss_mpi_sys_bind(&vi_output, &vpss_input);
```

绑定之后：

```text
VI Channel YUV -> VPSS Group
```

由硬件自动完成，不需要 CPU 中转。

如果使用 CPU 主动调用 `get_frame()` 再调用 `send_frame()`，会增加：

- CPU 开销；
- 内存带宽；
- 缓冲区管理复杂度；
- 延迟。

所以在固定硬件 Pipeline 中，优先使用 `bind`。

---

## 8. VPSS 如何进入 VENC

主码流的 VENC 输入来自 VPSS：

```cpp
ot_mpp_chn vpss_output{};
vpss_output.mod_id = OT_ID_VPSS;

ot_mpp_chn venc_input{};
venc_input.mod_id = OT_ID_VENC;

ss_mpi_sys_bind(&vpss_output, &venc_input);
```

建立连接后，VPSS 输出的 YUV 会自动进入编码器。

VENC 的主要启动调用：

```cpp
ss_mpi_venc_create_chn(venc_chn, &venc_attr);
ss_mpi_venc_start_chn(venc_chn, &start_param);
```

其中 `venc_attr` 描述：

- H.264 或 H.265；
- 输出宽高；
- Profile；
- GOP；
- CBR/AVBR；
- 输入和输出帧率；
- 目标码率；
- 编码缓冲区大小。

---

## 9. 什么时候开始“读取视频”

这个项目中 `start_capture(true)` 的名字容易误解。

它不是开始读取 RAW，而是启动“读取 VENC 编码结果”的线程。

正式代码中的取流线程大致执行：

```text
select(venc_fd)
    |
    v
ss_mpi_venc_query_status()
    |
    v
ss_mpi_venc_get_stream()
    |
    v
process_video_stream()
    |
    v
通知 EncodedStreamChannel
    |
    v
ss_mpi_venc_release_stream()
```

### 9.1 为什么先 select

`ss_mpi_venc_get_fd()` 可以获得 VENC 的文件描述符。

应用使用 `select()` 等待 fd 可读，可以避免不断轮询编码器状态。

### 9.2 get_stream 和 release_stream 必须成对

典型流程：

```cpp
ss_mpi_venc_get_stream(venc_chn, &stream, TD_TRUE);

// 在这里消费或复制编码数据

ss_mpi_venc_release_stream(venc_chn, &stream);
```

`get_stream()` 返回的 `pack.addr` 指向 VENC 管理的临时缓冲区。

如果忘记 `release_stream()`：

- VENC 缓冲区无法回收；
- 可用缓冲越来越少；
- 最终编码器可能停止出流。

如果在 `release_stream()` 之后仍保存 `pack.addr`：

- 指针已经失效；
- 后续读取属于非法内存访问或脏数据。

因此需要长期保存或异步发送时，必须先深拷贝。

---

## 10. 编码结果如何进入 MediaSource

正式项目的路径是：

```text
ss_mpi_venc_get_stream()
    |
    v
venc_h264::process_video_stream()
    |
    v
post_stream_to_observer()
    |
    v
EncodedStreamChannel::on_stream_come()
    |
    v
copy_encoded_frame()
    |
    v
MediaSource::publish()
```

### 10.1 VENC 层整理 NALU

`process_video_stream()` 遍历海思 `ot_venc_pack`，整理出：

- NALU 地址；
- NALU 长度；
- PTS；
- H.264/H.265 类型。

### 10.2 EncodedStreamChannel 适配项目媒体模型

回调中会确定：

- `stream_id`；
- 视频或音频；
- H.264/H.265 编码；
- 关键帧或预测帧；
- PTS/DTS；
- 所有 NALU chunk。

### 10.3 深拷贝为 MediaFrame

`copy_encoded_frame()` 创建属于应用自己的 `MediaFrame`，然后执行：

```cpp
std::memcpy(frame->data() + offset, chunk.data, chunk.size);
```

这是整个生命周期转换的关键点：

```text
VENC 临时缓冲区
    |
    | 深拷贝
    v
应用拥有的 MediaFrame
```

完成深拷贝后，海思 VENC 缓冲区才能安全释放。

### 10.4 MediaSource::publish()

最后调用：

```cpp
media_source_->publish(std::move(frame));
```

`MediaSource` 会做两件事：

1. 视频帧进入 GOP Cache；
2. 把帧通知给当前 `stream_id` 的订阅者。

GOP Cache 让新加入的观看者可以先拿到最近的关键帧和后续预测帧，不必等待下一次完整 GOP。

### 10.5 同一个 stream_id 如何同时包含音频和视频

这里的“包含音视频”不是说一个 `MediaFrame` 同时装着音频和视频，也不是一次回调同时携带两种数据。

正确模型是：

```text
stream_id = 1
  |-- Video Track：一系列 H.264/H.265 视频帧
  `-- Audio Track：一系列 G711U/AAC 音频帧
```

音频帧和视频帧是两个独立的时间序列。它们会分别产生回调，只是共用同一个回调函数签名：

```cpp
on_stream_come(...)
```

可能出现的调用顺序类似：

```text
回调 1：AudioFrame，PTS=1000 ms
回调 2：VideoFrame，PTS=1000 ms
回调 3：AudioFrame，PTS=1020 ms
回调 4：VideoFrame，PTS=1066 ms
回调 5：AudioFrame，PTS=1040 ms
```

音视频帧率不同，所以不会严格一帧音频对应一帧视频。同步依赖各自的 PTS，而不是依赖回调顺序。

当前取流线程同时把 VENC fd 和 AENC fd 放进 `select()`：

```text
VENC fd 可读 -> 获取一帧编码视频 -> 视频回调
AENC fd 可读 -> 获取一帧编码音频 -> 音频回调
```

即使某次 `select()` 同时发现两个 fd 可读，代码也只是顺序执行两次独立处理，不会把它们合成一次回调。

`EncodedStreamChannel::on_stream_come()` 使用 `head->type` 判断本次回调是什么：

```cpp
if (head->type == STREAM_AUDIO_FRAME) {
    // 本次只有音频数据，使用 buf + len。
} else {
    // 本次只有视频 NALU，使用 head->nalu[]。
}
```

随后每次回调只创建一个 `MediaFrame`：

```text
音频回调 -> MediaFrame(type=Audio, stream_id=1)
视频回调 -> MediaFrame(type=Video, stream_id=1)
```

`MediaSource` 按 `stream_id` 保存状态，但每个状态中分别保存音频和视频 Track：

```cpp
struct StreamState {
    std::optional<StreamTrack> video_track;
    std::optional<StreamTrack> audio_track;
};
```

所以订阅 `stream_id == 1` 后，同一个订阅回调会先后收到该流的音频帧和视频帧。WebRTC 再根据类型分流：

```cpp
if (frame->type() == MediaType::Audio) {
    enqueue_audio(frame);
} else if (frame->type() == MediaType::Video) {
    enqueue_video(frame);
}
```

当前项目只有一个 AENC 音频源。取到一帧音频后，代码会遍历所有正在运行的 VENC 流，把同一份音频分别映射到这些 `stream_id`：

```text
一帧 AENC 音频
  |-- 发布为 stream 0 的 AudioFrame
  `-- 发布为 stream 1 的 AudioFrame（子码流正在运行时）
```

因此主码流和子码流的视频内容、分辨率可以不同，但通常共用同一份麦克风音频。这里是逻辑映射，并不是麦克风被采集了两次。

---

## 11. 主码流、子码流的启动策略

当前项目采用：

```text
主码流：程序启动后一直编码
子码流：有订阅者时启动，空闲后停止
AI 流 ：有需求时异步启动，空闲后停止
```

主码流在 `chn::start()` 中直接启动：

```cpp
m_venc_main_ptr->start();
```

子码流初始只执行：

```cpp
m_venc_sub_ptr->prepare();
```

`prepare()` 只创建 VENC Channel，并没有持续编码。

当 WebRTC 首次订阅子码流时：

```text
MediaSource::subscribe(stream_id = 1)
    |
    v
StreamDemandHandler(..., active = true)
    |
    v
HisiliconPipeline::set_stream_active()
    |
    v
set_sub_stream_enabled(true)
```

这样可以减少没有观看者时的编码负载和内存占用。

当前默认配置：

```json
"preview_stream_id": 1
```

所以 WebRTC 默认预览子码流。

---

## 12. 参数怎样理解

教学 Demo 使用的主要参数：

```cpp
kSystemFlags
kLaneMode
kSensorWidth
kSensorHeight
kSensorFps
kWdrMode
kChannelId
kEncodeWidth
kEncodeHeight
kEncodeFps
kBitrateKbps
kSvcEnabled
```

### 12.1 输入侧参数

```cpp
kSensorWidth  = 2560;
kSensorHeight = 1440;
kSensorFps    = 30;
```

这些描述 Sensor、MIPI、VI 和 ISP 输入侧能力。

### 12.2 输出侧参数

```cpp
kEncodeWidth  = 1280;
kEncodeHeight = 720;
kEncodeFps    = 15;
```

这些描述最终交给主 VENC 的视频尺寸和帧率。

所以：

```text
Sensor 输入：2560x1440 @ 30 fps RAW
编码输出  ：1280x720  @ 15 fps H.264
```

中间由 ISP 和 VPSS 完成格式处理、缩放与帧率转换。

### 12.3 kSystemFlags

它是功能位集合，不是普通数值。不同 bit 用于打开：

- AIISP；
- LDC；
- 去呼吸效应；
- WDR 等功能。

`0` 表示不启用这些额外功能。

### 12.4 kLaneMode

它描述 MIPI 高速 lane 的分配模式，需要与：

- 板卡原理图；
- Sensor 接线；
- MIPI RX 配置；
- Sensor 驱动参数

保持一致。

### 12.5 kWdrMode

WDR 是宽动态范围。`0` 表示关闭。

开启 WDR 通常需要同时修改：

- Sensor 输出模式；
- MIPI WDR 模式；
- VI Pipe 数量；
- VI fusion group；
- ISP WDR 属性；
- VB 内存规划。

不能只修改一个整数。

### 12.6 kBitrateKbps

CBR 目标码率，单位是 `kbit/s`，不是 `byte/s`。

例如：

```text
800 kbps = 800 kbit/s
```

### 12.7 kSvcEnabled

SVC 是分层视频编码。`0` 表示使用普通 H.264 编码路径。

开启后需要编码器和接收端共同支持相应的分层码流策略。

---

## 13. demo_1.cpp 怎样阅读

[demo_1.cpp](./demo_1.cpp) 使用项目已有的底层封装，目标是先理解 Pipeline，而不是先陷入大量 SDK 属性结构。

它使用：

```cpp
hisilicon::dev::chn::init(...);
pipeline->start(...);
hisilicon::dev::chn::start_capture(true);
```

内部仍然执行真实的：

```text
MPP/VB -> MIPI -> VI -> ISP -> VPSS -> VENC -> get_stream
```

但细节由 `dev::chn`、`vi_isp`、`venc` 封装。

编码结果进入 `EncodedFileSink::on_stream_come()`，然后写到：

```text
/tmp/demo_1.h264
```

这个 Demo 适合回答：

- 整体顺序是什么；
- 什么时候启动硬件链路；
- 什么时候开始取编码流；
- 编码回调里拿到什么；
- 资源怎样逆序关闭。

注意：Demo 在编码回调里直接 `fwrite()` 只适合教学。文件写入可能阻塞，生产代码应该尽快复制码流，再交给其他线程处理。

---

## 14. demo_2.cpp 怎样阅读

[demo_2.cpp](./demo_2.cpp) 绕过项目设备封装，直接调用海思 SDK。

它按 A 到 K 分块：

```text
A. 固定参数和模块编号
B. 资源状态
C. 准备各模块属性
D. 初始化 VB 和 MPP System
E. 配置 MIPI RX 和 Sensor 时钟
F. 启动 VI
G. 启动 Sensor 回调、AE、AWB 和 ISP
H. 启动 VPSS
I. 启动 VENC
J. 应用层获取编码码流
K. 按反方向释放资源
```

这个 Demo 适合回答：

- 每个海思属性结构配置了什么；
- 每个 `ss_mpi_*` API 在何时调用；
- 模块怎样 bind；
- ISP 线程为什么存在；
- VENC fd 怎样等待；
- `get_stream/release_stream` 怎样配对；
- 没有 C++ 封装时需要记录哪些资源状态。

它将 H.264 写到：

```text
/tmp/demo_2.h264
```

可以在设备上尝试：

```bash
ffplay -f h264 /tmp/demo_2.h264
```

`demo_2.cpp` 没有加入 `board/Makefile`，因为正式程序已经有 `app/main.cpp`，两个 `main()` 不能同时链接。

---

## 15. 推荐的代码阅读顺序

第一遍，只理解整体：

```text
app/demo_1.cpp
```

第二遍，理解直接 SDK 调用：

```text
app/demo_2.cpp
```

第三遍，对照正式封装：

```text
device_adapter/src/hisilicon_pipeline.cpp
devices/hisi_3516cv610/dev_chn.cpp
devices/hisi_3516cv610/dev_vi_isp.cpp
devices/hisi_3516cv610/dev_venc.cpp
```

第四遍，理解编码数据如何进入业务层：

```text
device_adapter/src/encoded_stream_channel.cpp
device_adapter/src/encoded_frame_adapter.cpp
src/media/media_source.cpp
```

第五遍，再研究具体 Sensor：

```text
devices/hisi_3516cv610/dev_vi_sc4336p_liner.cpp
devices/hisi_3516cv610/sensor/smart_sc4336p/
```

推荐每次只追一条线：

```text
启动线：main -> Application -> Pipeline -> VI/ISP/VPSS/VENC
数据线：VENC -> callback -> MediaFrame -> MediaSource -> WebRTC
停止线：WebRTC -> VENC -> VPSS -> ISP -> VI -> MIPI -> MPP/VB
```

---

## 16. 常见误区

### 误区一：start_capture() 是开始读取 RAW

不是。当前封装中的 `start_capture(true)` 是启动 VENC 编码码流读取线程。

RAW 在更早的 MIPI、VI、ISP 启动过程中已经进入硬件 Pipeline。

### 误区二：YUV 一定会出现在应用内存中

不一定。

当前使用 `bind`，YUV 可以直接在 VI、VPSS、VENC 硬件模块之间流动，应用不需要拿到 YUV 指针。

### 误区三：channel_id 表示音频或视频

不是。`channel_id` 表示逻辑摄像头/采集 Pipeline；音视频由 `MediaType` 区分。

### 误区四：Device 0、Pipe 0、Channel 0 是同一个 0

不是。它们属于三个不同模块层级的编号空间。

### 误区五：修改 Sensor 名称或宽高就能换摄像头

通常不行。更换 Sensor 还涉及：

- MIPI lane；
- RAW 位宽；
- Bayer 排列；
- 时钟；
- I2C 地址和寄存器；
- Sensor 驱动对象；
- ISP 参数；
- 分辨率和帧率模式。

### 误区六：get_stream() 后保存 pack.addr 就可以异步发送

不可以。`pack.addr` 属于 VENC，调用 `release_stream()` 后就不能继续使用。异步处理前必须深拷贝。

### 误区七：忘记 release_stream() 只是小泄漏

不是。它会占住 VENC 码流缓冲，最终导致编码器停止正常出流。

---

## 17. 最终记忆模型

可以用工厂模型记忆整条链路：

```text
Sensor       = 原料来源
MIPI         = 原料运输线路
VI Device    = 工厂大门
VI Pipe      = RAW/ISP 生产线
VI Channel   = YUV 出货口
VPSS         = 缩放、裁剪等加工车间
VENC         = H.264/H.265 包装车间
get_stream   = 取走包装完成的产品
MediaSource  = 产品仓库和分发中心
WebRTC       = 对外运输渠道
```

再压缩成一句话：

> Sensor 通过 MIPI 把 RAW 送入 VI Device，VI Pipe 和 ISP 将 RAW 处理成 YUV，由 VI Channel 输出给 VPSS，VPSS 将合适尺寸的 YUV 送给 VENC，应用从 VENC 取出 H.264/H.265，深拷贝成 MediaFrame 后发布到 MediaSource。
