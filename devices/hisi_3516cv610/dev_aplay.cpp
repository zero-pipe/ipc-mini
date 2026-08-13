#include "dev_aplay.h"
#include "dev_log.h"

#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

namespace hisilicon {
namespace dev {
namespace {

constexpr uint8_t kMulawSilence = 0xFF;
constexpr unsigned kSilenceFramesToStop = 50; /* ~1s @ 20ms */

bool is_mulaw_silence_frame(const uint8_t* data, size_t len)
{
    if (!data || len == 0) {
        return true;
    }
    unsigned quiet = 0;
    for (size_t i = 0; i < len; ++i) {
        const unsigned d = static_cast<unsigned>(kMulawSilence - data[i]);
        if (d <= 2 || data[i] <= 2) {
            ++quiet;
        }
    }
    return quiet * 10 >= len * 9;
}

int16_t mulaw_to_linear(uint8_t mulaw)
{
    mulaw = ~mulaw;
    const int sign = mulaw & 0x80;
    const int exponent = (mulaw >> 4) & 0x07;
    const int mantissa = mulaw & 0x0f;
    int sample = ((mantissa << 3) + 0x84) << exponent;
    sample -= 0x84;
    return static_cast<int16_t>(sign ? -sample : sample);
}

} // namespace

aplay& aplay::instance()
{
    static aplay g;
    return g;
}

aplay::~aplay()
{
    stop();
}

bool aplay::started() const
{
    std::lock_guard lock(mutex_);
    return started_;
}

bool aplay::configure_acodec_output()
{
    const int fd = open("/dev/acodec", O_RDWR);
    if (fd < 0) {
        DEV_WRITE_LOG_ERROR("open /dev/acodec failed for AO");
        return false;
    }

    /*
     * Do not soft-reset: AI/AENC already owns the shared inner codec.
     * INNER sample does not use SET_OUTPUT_VOLUME; wake DAC for LINEOUT.
     */
    auto try_ioctl = [fd](unsigned long req, void* arg, const char* name) {
        const td_s32 ret = ioctl(fd, req, arg);
        if (ret != TD_SUCCESS) {
            DEV_WRITE_LOG_WARN("%s failed %#x (continue)", name, ret);
        }
        return ret;
    };

    td_u32 zero = 0;
    td_u32 one = 1;
    try_ioctl(OT_ACODEC_SET_PD_DACL, &zero, "SET_PD_DACL");
    try_ioctl(OT_ACODEC_SET_PD_DACR, &zero, "SET_PD_DACR");
    try_ioctl(OT_ACODEC_SET_DACL_MUTE, &zero, "SET_DACL_MUTE");
    try_ioctl(OT_ACODEC_SET_DACR_MUTE, &zero, "SET_DACR_MUTE");
    try_ioctl(OT_ACODEC_DAC_SOFT_UNMUTE, &one, "DAC_SOFT_UNMUTE");

    ot_acodec_volume_ctrl dac_vol {};
    dac_vol.volume_ctrl = 0x5a;
    dac_vol.volume_ctrl_mute = 0;
    try_ioctl(OT_ACODEC_SET_DACL_VOLUME, &dac_vol, "SET_DACL_VOLUME");
    try_ioctl(OT_ACODEC_SET_DACR_VOLUME, &dac_vol, "SET_DACR_VOLUME");

    /* Optional on this board; failure must not block AO start. */
    td_u32 output_vol = 40;
    try_ioctl(OT_ACODEC_SET_OUTPUT_VOLUME, &output_vol, "SET_OUTPUT_VOLUME");

    close(fd);
    return true;
}

bool aplay::send_pcm_locked(const int16_t* pcm, td_u32 bytes)
{
    ot_audio_frame frame {};
    frame.bit_width = OT_AUDIO_BIT_WIDTH_16;
    frame.snd_mode = OT_AUDIO_SOUND_MODE_MONO;
    frame.virt_addr[0] = reinterpret_cast<td_u8*>(const_cast<int16_t*>(pcm));
    frame.virt_addr[1] = nullptr;
    frame.phys_addr[0] = 0;
    frame.phys_addr[1] = 0;
    frame.time_stamp = 0;
    frame.seq = 0;
    frame.len = bytes; /* bytes per channel */
    frame.pool_id[0] = 0;
    frame.pool_id[1] = 0;

    const td_s32 ret = ss_mpi_ao_send_frame(ao_dev_, ao_chn_, &frame, 1000);
    return ret == TD_SUCCESS;
}

bool aplay::start()
{
    std::lock_guard lock(mutex_);
    if (started_) {
        return true;
    }

    std::memset(&aio_attr_, 0, sizeof(aio_attr_));
    aio_attr_.sample_rate = OT_AUDIO_SAMPLE_RATE_8000;
    aio_attr_.snd_mode = OT_AUDIO_SOUND_MODE_MONO;
    aio_attr_.chn_cnt = 1;
    /* 160 samples @ 8kHz = 20ms — match AI/AENC and WebRTC PCMU. */
    aio_attr_.point_num_per_frame = 160;
    aio_attr_.bit_width = OT_AUDIO_BIT_WIDTH_16;
    aio_attr_.work_mode = OT_AIO_MODE_I2S_MASTER;
    aio_attr_.expand_flag = 0;
    aio_attr_.frame_num = 5;
    aio_attr_.clk_share = 1;
    aio_attr_.i2s_type = OT_AIO_I2STYPE_INNERCODEC;

    td_s32 ret = ss_mpi_ao_set_pub_attr(ao_dev_, &aio_attr_);
    if (ret != TD_SUCCESS) {
        DEV_WRITE_LOG_ERROR("ss_mpi_ao_set_pub_attr failed %#x", ret);
        return false;
    }

    ret = ss_mpi_ao_enable(ao_dev_);
    if (ret != TD_SUCCESS) {
        DEV_WRITE_LOG_ERROR("ss_mpi_ao_enable failed %#x", ret);
        return false;
    }

    ret = ss_mpi_ao_enable_chn(ao_dev_, ao_chn_);
    if (ret != TD_SUCCESS) {
        DEV_WRITE_LOG_ERROR("ss_mpi_ao_enable_chn failed %#x", ret);
        ss_mpi_ao_disable(ao_dev_);
        return false;
    }

    /* Official sample always enables the AO system channel as well. */
    ret = ss_mpi_ao_enable_chn(ao_dev_, OT_AO_SYS_CHN_ID);
    if (ret != TD_SUCCESS) {
        DEV_WRITE_LOG_ERROR("ss_mpi_ao_enable_chn(SYS) failed %#x", ret);
        ss_mpi_ao_disable_chn(ao_dev_, ao_chn_);
        ss_mpi_ao_disable(ao_dev_);
        return false;
    }

    if (!configure_acodec_output()) {
        ss_mpi_ao_disable_chn(ao_dev_, OT_AO_SYS_CHN_ID);
        ss_mpi_ao_disable_chn(ao_dev_, ao_chn_);
        ss_mpi_ao_disable(ao_dev_);
        return false;
    }

    td_s32 vol_ret = ss_mpi_ao_set_volume(ao_dev_, 6);
    if (vol_ret != TD_SUCCESS) {
        DEV_WRITE_LOG_WARN("ss_mpi_ao_set_volume(6) failed %#x", vol_ret);
    }
    (void)ss_mpi_ao_set_mute(ao_dev_, TD_FALSE, nullptr);

    silence_frames_ = 0;
    started_ = true;
    DEV_WRITE_LOG_INFO("aplay AO PCM started points=160 ao_vol=6");
    return true;
}

void aplay::stop()
{
    std::lock_guard lock(mutex_);
    if (!started_) {
        return;
    }

    ss_mpi_ao_disable_chn(ao_dev_, OT_AO_SYS_CHN_ID);
    ss_mpi_ao_disable_chn(ao_dev_, ao_chn_);
    ss_mpi_ao_disable(ao_dev_);
    silence_frames_ = 0;
    started_ = false;
}

bool aplay::send_g711u(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > 960) {
        return false;
    }

    const bool silence = is_mulaw_silence_frame(data, len);
    bool need_start = false;
    bool stop_for_idle = false;
    {
        std::lock_guard lock(mutex_);
        if (!started_) {
            /* Muted browser tracks still send µ-law silence — do not open AO. */
            if (silence) {
                return true;
            }
            need_start = true;
        } else if (silence) {
            stop_for_idle = (++silence_frames_ >= kSilenceFramesToStop);
        } else {
            silence_frames_ = 0;
        }
    }

    if (need_start && !start()) {
        return false;
    }
    if (stop_for_idle) {
        DEV_WRITE_LOG_INFO("aplay idle silence — stopping AO");
        stop();
        return true;
    }

    std::vector<int16_t> pcm(len);
    for (size_t i = 0; i < len; ++i) {
        pcm[i] = mulaw_to_linear(data[i]);
    }

    bool ok = false;
    {
        std::lock_guard lock(mutex_);
        if (!started_) {
            return false;
        }
        ok = send_pcm_locked(pcm.data(), static_cast<td_u32>(len * sizeof(int16_t)));
    }
    if (!ok) {
        static unsigned fail_n = 0;
        if (++fail_n == 1 || (fail_n % 100) == 0) {
            DEV_WRITE_LOG_ERROR("ao_send_frame fail count=%u len=%zu", fail_n, len);
        }
        return false;
    }
    static unsigned ok_n = 0;
    if (++ok_n == 1) {
        DEV_WRITE_LOG_INFO("ao playback started len=%zu", len);
    }
    return true;
}

} // namespace dev
} // namespace hisilicon
