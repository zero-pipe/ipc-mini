#include "dev_sys.h"
#include "dev_aenc.h"
#include "dev_log.h"

namespace hisilicon{namespace dev{

    aenc::aenc(ot_audio_dev ai_dev,ot_ai_chn ai_chn,ot_aenc_chn aenc_chn,bool is_mic)
        :m_ai_dev(ai_dev),m_ai_chn(ai_chn),m_aenc_chn(aenc_chn),m_is_mic(is_mic)
    {
        m_is_start = false;
    }

    aenc::~aenc()
    {
    }

    uint8_t aenc::chn_cnt()
    {
        return (m_aio_attr.snd_mode == OT_AUDIO_SOUND_MODE_MONO) ? 1 : 2;
    }

    uint8_t aenc::bit_width()
    {
        return 16;
    }

    uint32_t aenc::sample_rate()
    {
        if(m_aio_attr.sample_rate  == OT_AUDIO_SAMPLE_RATE_48000)
        {
            return 48000;
        }
        else if(m_aio_attr.sample_rate == OT_AUDIO_SAMPLE_RATE_8000)
        {
            return 8000;
        }
        else if(m_aio_attr.sample_rate  == OT_AUDIO_SAMPLE_RATE_44100)
        {
            return 44100;
        }

        return 8000;
    }

    int32_t aenc::aenc_fd()
    {
        if(!m_is_start)
        {
            return -1;
        }

        return ss_mpi_aenc_get_fd(m_aenc_chn);
    }

    void aenc::release_stream(ot_audio_stream *stream)
    {
        td_s32 ret;
        ret = ss_mpi_aenc_release_stream(m_aenc_chn,stream);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("release aenc stream failed");
        }
    }

    bool aenc::get_stream(ot_audio_stream *stream, td_s32 msec)
    {
        if(!m_is_start)
        {
            return false;
        }

        td_s32 ret;
        ret = ss_mpi_aenc_get_stream(m_aenc_chn,stream,msec);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("get aenc stream failed");
            return false;
        }

        return true;
    }

    bool aenc::get_instance(const char* name,bool is_mic,aenc_ptr& aenc)
    {
        std::string aec_name = name;

        static std::mutex g_mu;

        std::unique_lock<std::mutex> lock(g_mu);
        if(aec_name == "G711U")
        {
            aenc = std::make_shared<aenc_g711u>(is_mic);
            return true;
        }
        else if(aec_name == "AAC")
        {
            aenc = std::make_shared<aenc_aac>(is_mic);
            return true;
        }

        return false;
    }

    bool aenc::start()
    {
        if(m_is_start)
        {
            return false;
        }

        int32_t fd = -1;
        ot_acodec_mixer input_mode;
        td_s32 ret;
        ot_mpp_chn src_chn, dest_chn;
        int32_t acodec_input_vol;

        ret = ss_mpi_ai_set_pub_attr(m_ai_dev, &m_aio_attr);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_ai_set_pub_attr failed with error %#x",ret);
            return false;
        }

        ret = ss_mpi_ai_enable(m_ai_dev);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_ai_enable failed with error %#x",ret);
            return false;
        }

        ret = ss_mpi_ai_enable_chn(m_ai_dev, m_ai_chn);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_ai_enable failed with error %#x",ret);
            goto ERR;
        }

        fd = open("/dev/acodec", O_RDWR);
        if(fd < 0)
        {
            DEV_WRITE_LOG_ERROR("open /dev/acocdec faile");
            goto ERR;
        }

        ret = ioctl(fd, OT_ACODEC_SOFT_RESET_CTRL);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("acodec ioctl failed");
            goto ERR;
        }

        ret = ioctl(fd, OT_ACODEC_SET_I2S1_FS, &m_i2s_fs_sel);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("acodec ioctl failed");
            goto ERR;
        }

        /* refer to hardware, demo board is pseudo-differential (IN_D), socket board is single-ended (IN1) */
        input_mode = OT_ACODEC_MIXER_IN_D;
        ret = ioctl(fd, OT_ACODEC_SET_MIXER_MIC, &input_mode);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("acodec ioctl failed");
            goto ERR;
        }

        if(m_is_mic)
        {
            /*
             * The input volume range is [-78, 80]. Both the analog gain and digital gain are adjusted.
             * A larger value indicates higher volume.
             * For example, the value 80 indicates the maximum volume of 80 dB,
             * and the value -78 indicates the minimum volume (muted status).
             * The volume adjustment takes effect simultaneously in the audio-left and audio-right channels.
             * The recommended volume range is [20, 50].
             * Within this range, the noises are lowest because only the analog gain is adjusted,
             * and the voice quality can be guaranteed.
             */
            acodec_input_vol = 30; /* 30dB */
            ret = ioctl(fd,OT_ACODEC_SET_INPUT_VOLUME, &acodec_input_vol);
            if (ret != TD_SUCCESS) 
            {
                DEV_WRITE_LOG_ERROR("set acodec micin volume failed");
                goto ERR;
            }
        }

        ret = ss_mpi_aenc_create_chn(m_aenc_chn, &m_aenc_attr);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_aenc_create_chn failed with:%#x",ret);
            goto ERR;
        }

        src_chn.mod_id = OT_ID_AI;
        src_chn.dev_id = m_ai_dev;
        src_chn.chn_id = m_ai_chn;
        dest_chn.mod_id = OT_ID_AENC;
        dest_chn.dev_id = 0;
        dest_chn.chn_id = m_aenc_chn;
        ret = ss_mpi_sys_bind(&src_chn, &dest_chn);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_sys_bind failed with:%#x",ret);
            goto ERR;
        }

        close(fd);
        fd = -1;
        m_is_start = true;
        return true;
ERR:
        if(fd > -1)
        {
            close(fd);
            fd = -1;
        }
        ss_mpi_aenc_destroy_chn(m_aenc_chn);
        ss_mpi_ai_disable_chn(m_ai_dev,m_ai_chn);
        ss_mpi_ai_disable(m_ai_dev);
        return false;
    }

    void aenc::stop()
    {
        if(!m_is_start)
        {
            return;
        }

        ot_mpp_chn src_chn, dest_chn;
        src_chn.mod_id = OT_ID_AI;
        src_chn.dev_id = m_ai_dev;
        src_chn.chn_id = m_ai_chn;
        dest_chn.mod_id = OT_ID_AENC;
        dest_chn.dev_id = 0;
        dest_chn.chn_id = m_aenc_chn;
        ss_mpi_sys_unbind(&src_chn, &dest_chn);

        ss_mpi_aenc_destroy_chn(m_aenc_chn);
        ss_mpi_ai_disable_chn(m_ai_dev,m_ai_chn);
        ss_mpi_ai_disable(m_ai_dev);

        m_is_start = false;
    }

    aenc_g711u::aenc_g711u(bool is_mic)
                :aenc(0,0,0,is_mic)
    {
        memset(&m_aio_attr,0,sizeof(m_aio_attr));
        m_payload_type = OT_PT_G711U; 
        m_aio_attr.sample_rate  = OT_AUDIO_SAMPLE_RATE_8000;
        m_aio_attr.snd_mode     = OT_AUDIO_SOUND_MODE_MONO;
        m_aio_attr.chn_cnt      = 1;
        /* 160 samples @ 8kHz = 20ms — matches WebRTC/PCMU packetization. */
        m_aio_attr.point_num_per_frame = 160;
        m_aio_attr.bit_width    = OT_AUDIO_BIT_WIDTH_16;
        m_aio_attr.work_mode    = OT_AIO_MODE_I2S_MASTER;
        m_aio_attr.expand_flag  = 0;
        m_aio_attr.frame_num    = 5;
        m_aio_attr.clk_share  = 1;
        m_aio_attr.i2s_type   = OT_AIO_I2STYPE_INNERCODEC;

        m_aenc_attr.type = OT_PT_G711U;
        m_aenc_attr.buf_size = 30;
        m_aenc_attr.point_num_per_frame = m_aio_attr.point_num_per_frame;
        m_aenc_attr.value = &m_aenc_g711;

        m_i2s_fs_sel = OT_ACODEC_FS_8000;
    }

    aenc_g711u::~aenc_g711u()
    {
        stop();
    }

    aenc_aac::aenc_aac(bool is_mic)
                :aenc(0,0,0,is_mic)
    {
        memset(&m_aio_attr,0,sizeof(m_aio_attr));
        m_payload_type = OT_PT_AAC; 
        m_aio_attr.sample_rate  = OT_AUDIO_SAMPLE_RATE_44100;
        m_aio_attr.snd_mode     = OT_AUDIO_SOUND_MODE_STEREO;
        m_aio_attr.chn_cnt      = 2;
        m_aio_attr.point_num_per_frame = OT_AACLC_SAMPLES_PER_FRAME;
        m_aio_attr.bit_width    = OT_AUDIO_BIT_WIDTH_16;
        m_aio_attr.work_mode    = OT_AIO_MODE_I2S_MASTER;
        m_aio_attr.expand_flag  = 0;
        m_aio_attr.frame_num    = 5;
        m_aio_attr.clk_share  = 1;
        m_aio_attr.i2s_type   = OT_AIO_I2STYPE_INNERCODEC;

        m_aenc_attr.type = OT_PT_AAC;
        m_aenc_attr.buf_size = 30;
        m_aenc_attr.point_num_per_frame = m_aio_attr.point_num_per_frame;
        m_aenc_attr.value = &m_aenc_aac;
        
        memset(&m_aenc_aac,0,sizeof(m_aenc_aac));
        m_aenc_aac.aac_type = OT_AAC_TYPE_AACLC;
        m_aenc_aac.bit_rate = OT_AAC_BPS_96K;
        m_aenc_aac.bit_width = OT_AUDIO_BIT_WIDTH_16;
        m_aenc_aac.sample_rate = m_aio_attr.sample_rate;
        m_aenc_aac.snd_mode = m_aio_attr.snd_mode;
        m_aenc_aac.transport_type = OT_AAC_TRANSPORT_TYPE_ADTS;
        m_aenc_aac.band_width = 0;

        m_i2s_fs_sel = OT_ACODEC_FS_44100;
    }

    aenc_aac::~aenc_aac()
    {
        stop();
    }

}}//namespace


