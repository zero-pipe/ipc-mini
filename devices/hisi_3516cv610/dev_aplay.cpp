#include "dev_aplay.h"
#include "dev_log.h"

#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

namespace hisilicon {
namespace dev {
namespace {

/* HiSilicon G711 frame header: payload length in byte2 (len < 256). */
constexpr uint8_t kG711Header0 = 0x00;
constexpr uint8_t kG711Header1 = 0x01;

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

    /* Do not soft-reset: AI/AENC already configured the shared inner codec. */
    td_u32 output_vol = 6; /* modest default */
    td_s32 ret = ioctl(fd, OT_ACODEC_SET_OUTPUT_VOLUME, &output_vol);
    if (ret != TD_SUCCESS) {
        DEV_WRITE_LOG_ERROR("OT_ACODEC_SET_OUTPUT_VOLUME failed %#x", ret);
        close(fd);
        return false;
    }

    close(fd);
    return true;
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
    aio_attr_.point_num_per_frame = 480;
    aio_attr_.bit_width = OT_AUDIO_BIT_WIDTH_16;
    aio_attr_.work_mode = OT_AIO_MODE_I2S_MASTER;
    aio_attr_.expand_flag = 0;
    aio_attr_.frame_num = 5;
    aio_attr_.clk_share = 1;
    aio_attr_.i2s_type = OT_AIO_I2STYPE_INNERCODEC;

    std::memset(&adec_attr_, 0, sizeof(adec_attr_));
    adec_attr_.type = OT_PT_G711U;
    adec_attr_.buf_size = 20;
    adec_attr_.mode = OT_ADEC_MODE_STREAM;
    adec_attr_.value = &adec_g711_;

    td_s32 ret = ss_mpi_adec_create_chn(adec_chn_, &adec_attr_);
    if (ret != TD_SUCCESS) {
        DEV_WRITE_LOG_ERROR("ss_mpi_adec_create_chn failed %#x", ret);
        return false;
    }

    ret = ss_mpi_ao_set_pub_attr(ao_dev_, &aio_attr_);
    if (ret != TD_SUCCESS) {
        DEV_WRITE_LOG_ERROR("ss_mpi_ao_set_pub_attr failed %#x", ret);
        ss_mpi_adec_destroy_chn(adec_chn_);
        return false;
    }

    ret = ss_mpi_ao_enable(ao_dev_);
    if (ret != TD_SUCCESS) {
        DEV_WRITE_LOG_ERROR("ss_mpi_ao_enable failed %#x", ret);
        ss_mpi_adec_destroy_chn(adec_chn_);
        return false;
    }

    ret = ss_mpi_ao_enable_chn(ao_dev_, ao_chn_);
    if (ret != TD_SUCCESS) {
        DEV_WRITE_LOG_ERROR("ss_mpi_ao_enable_chn failed %#x", ret);
        ss_mpi_ao_disable(ao_dev_);
        ss_mpi_adec_destroy_chn(adec_chn_);
        return false;
    }

    ot_mpp_chn src {};
    ot_mpp_chn dest {};
    src.mod_id = OT_ID_ADEC;
    src.dev_id = 0;
    src.chn_id = adec_chn_;
    dest.mod_id = OT_ID_AO;
    dest.dev_id = ao_dev_;
    dest.chn_id = ao_chn_;
    ret = ss_mpi_sys_bind(&src, &dest);
    if (ret != TD_SUCCESS) {
        DEV_WRITE_LOG_ERROR("bind ADEC→AO failed %#x", ret);
        ss_mpi_ao_disable_chn(ao_dev_, ao_chn_);
        ss_mpi_ao_disable(ao_dev_);
        ss_mpi_adec_destroy_chn(adec_chn_);
        return false;
    }

    if (!configure_acodec_output()) {
        ss_mpi_sys_unbind(&src, &dest);
        ss_mpi_ao_disable_chn(ao_dev_, ao_chn_);
        ss_mpi_ao_disable(ao_dev_);
        ss_mpi_adec_destroy_chn(adec_chn_);
        return false;
    }

    (void)ss_mpi_ao_set_volume(ao_dev_, 0);
    started_ = true;
    DEV_WRITE_LOG_INFO("aplay G711U started (ADEC→AO)");
    return true;
}

void aplay::stop()
{
    std::lock_guard lock(mutex_);
    if (!started_) {
        return;
    }

    ot_mpp_chn src {};
    ot_mpp_chn dest {};
    src.mod_id = OT_ID_ADEC;
    src.dev_id = 0;
    src.chn_id = adec_chn_;
    dest.mod_id = OT_ID_AO;
    dest.dev_id = ao_dev_;
    dest.chn_id = ao_chn_;
    ss_mpi_sys_unbind(&src, &dest);
    ss_mpi_ao_disable_chn(ao_dev_, ao_chn_);
    ss_mpi_ao_disable(ao_dev_);
    ss_mpi_adec_destroy_chn(adec_chn_);
    started_ = false;
}

bool aplay::send_g711u(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > 960) {
        return false;
    }

    bool need_start = false;
    {
        std::lock_guard lock(mutex_);
        need_start = !started_;
    }
    /* Lazy start so AI capture can run before AO claims the inner codec. */
    if (need_start && !start()) {
        return false;
    }

    std::vector<uint8_t> packet(len + 4);
    packet[0] = kG711Header0;
    packet[1] = kG711Header1;
    packet[2] = static_cast<uint8_t>(len & 0xff);
    packet[3] = static_cast<uint8_t>((len >> 8) & 0xff);
    std::memcpy(packet.data() + 4, data, len);

    ot_audio_stream stream {};
    stream.stream = packet.data();
    stream.len = static_cast<td_u32>(packet.size());
    stream.time_stamp = 0;
    stream.seq = 0;

    td_s32 ret = TD_FAILURE;
    {
        std::lock_guard lock(mutex_);
        if (!started_) {
            return false;
        }
        ret = ss_mpi_adec_send_stream(adec_chn_, &stream, TD_TRUE);
    }
    if (ret != TD_SUCCESS) {
        static unsigned fail_n = 0;
        if (++fail_n == 1 || (fail_n % 100) == 0) {
            DEV_WRITE_LOG_ERROR("adec send fail %#x count=%u len=%zu",
                                ret, fail_n, len);
        }
        return false;
    }
    static unsigned ok_n = 0;
    if (++ok_n == 1) {
        DEV_WRITE_LOG_INFO("adec playback started len=%zu", len);
    }
    return true;
}

} // namespace dev
} // namespace hisilicon
