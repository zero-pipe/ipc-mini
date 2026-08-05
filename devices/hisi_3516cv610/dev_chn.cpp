#include "dev_chn.h"
#include "dev_aplay.h"
#include "dev_log.h"

namespace hisilicon{namespace dev{

    std::shared_ptr<chn> chn::g_chns[MAX_CHANNEL];
    int32_t chn::g_sys_flag = 0;
    int32_t chn::g_vi_max_w = 0;
    int32_t chn::g_vi_max_h = 0;
    int32_t chn::g_vi_fr = 0;
    int32_t chn::g_vi_wdr_mode = 0;
    bool chn::g_aenc_enable  = false;
    std::string chn::g_aenc_name;

    chn::chn(const char* vi_name,const char* venc_mode,int32_t chn_no)
        :m_is_start(false),m_vi_name(vi_name),m_chn(chn_no),m_venc_mode(venc_mode)
    {
    }

    chn::~chn()
    {
        stop();
    }

    bool chn::start(int32_t venc_w,int32_t venc_h,int32_t fr,int32_t bitrate,
                    int32_t svc_enable,const time_osd_options& time_osd)
    {
        if(m_is_start || m_chn < 0 || m_chn >= MAX_CHANNEL)
        {
            DEV_WRITE_LOG_ERROR("invalid channel=%d", m_chn);
            return false;
        }

        if(m_vi_name == "SC4336P")
        {
            m_vi_ptr = std::make_shared<vi_sc4336p_liner>(g_sys_flag,g_vi_max_w,g_vi_max_h,g_vi_fr,g_vi_wdr_mode);
        }
        else if(m_vi_name == "SC431HAI")
        {
            m_vi_ptr = std::make_shared<vi_sc431hai_liner>(g_sys_flag,g_vi_max_w,g_vi_max_h,g_vi_fr,g_vi_wdr_mode);
        }
        else if(m_vi_name == "GC8613_4K")
        {
            m_vi_ptr = std::make_shared<vi_gc8613_4k20_liner>(g_sys_flag,g_vi_max_w,g_vi_max_h,g_vi_fr,g_vi_wdr_mode);
        }
        else if(m_vi_name == "GC8613_1080P")
        {
            m_vi_ptr = std::make_shared<vi_gc8613_1080p20_liner>(g_sys_flag,g_vi_max_w,g_vi_max_h,g_vi_fr,g_vi_wdr_mode);
        }
        else if(m_vi_name == "GC4023")
        {
            m_vi_ptr = std::make_shared<vi_gc4023_liner>(g_sys_flag,g_vi_max_w,g_vi_max_h,g_vi_fr,g_vi_wdr_mode);
        }
        else if(m_vi_name == "HY006_3814_0011")
        {
            m_vi_ptr = std::make_shared<vi_hy006_3814_0011_liner>(g_sys_flag,g_vi_max_w,g_vi_max_h,g_vi_fr,g_vi_wdr_mode);
        }
        else
        {
            DEV_WRITE_LOG_ERROR("unsupport sensor name=%s",m_vi_name.c_str());
            return false;
        }

        if(venc_w > m_vi_ptr->w()
                || venc_h > m_vi_ptr->h()
                || fr > m_vi_ptr->fr())
        {
            DEV_WRITE_LOG_ERROR("invalid param");
            return false;
        }

        // With wrap buffers, VPSS channel 0 must match the main encoder size.
        std::shared_ptr<vi_isp> viisp = std::dynamic_pointer_cast<vi_isp>(m_vi_ptr);
        if(viisp)
        {
            if(viisp->vpss_chn_attr().width != (td_u32)venc_w)
            {
                viisp->vpss_chn_attr().width = venc_w;
            }
            if(viisp->vpss_chn_attr().height != (td_u32)venc_h)
            {
                viisp->vpss_chn_attr().height = venc_h;
            }
        }

        if(!m_vi_ptr->start())
        {
            DEV_WRITE_LOG_ERROR("vi start failed");
            m_vi_ptr.reset();
            return false;
        }

        if(m_venc_mode == "H264_CBR")
        {
            m_venc_main_ptr = std::make_shared<venc_h264_cbr>(m_chn,MAIN_STREAM_ID,venc_w,venc_h,m_vi_ptr->fr(),fr,m_vi_ptr->vpss_grp(),m_vi_ptr->vpss_chn(),bitrate,svc_enable,-1);
            m_venc_sub_ptr  = std::make_shared<venc_h264_cbr>(m_chn,SUB_STREAM_ID,720,480,fr,fr,-1,-1,1000,0,m_venc_main_ptr->venc_chn());
        }
        else if(m_venc_mode == "H264_AVBR")
        {
            m_venc_main_ptr = std::make_shared<venc_h264_avbr>(m_chn,MAIN_STREAM_ID,venc_w,venc_h,m_vi_ptr->fr(),fr,m_vi_ptr->vpss_grp(),m_vi_ptr->vpss_chn(),bitrate,svc_enable,-1);
            m_venc_sub_ptr  = std::make_shared<venc_h264_avbr>(m_chn,SUB_STREAM_ID,720,480,fr,fr,-1,-1,1000,0,m_venc_main_ptr->venc_chn());
        }
        else if(m_venc_mode == "H265_CBR")
        {
            m_venc_main_ptr = std::make_shared<venc_h265_cbr>(m_chn,MAIN_STREAM_ID,venc_w,venc_h,m_vi_ptr->fr(),fr,m_vi_ptr->vpss_grp(),m_vi_ptr->vpss_chn(),bitrate,svc_enable,-1);
            m_venc_sub_ptr  = std::make_shared<venc_h265_cbr>(m_chn,SUB_STREAM_ID,720,480,fr,fr,-1,-1,1000,0,m_venc_main_ptr->venc_chn());
        }
        else if(m_venc_mode == "H265_AVBR")
        {
            m_venc_main_ptr = std::make_shared<venc_h265_avbr>(m_chn,MAIN_STREAM_ID,venc_w,venc_h,m_vi_ptr->fr(),fr,m_vi_ptr->vpss_grp(),m_vi_ptr->vpss_chn(),bitrate,svc_enable,-1);
            m_venc_sub_ptr  = std::make_shared<venc_h265_avbr>(m_chn,SUB_STREAM_ID,720,480,fr,fr,-1,-1,1000,0,m_venc_main_ptr->venc_chn());
        }
        else
        {
            DEV_WRITE_LOG_ERROR("invalid venc mode");
            m_vi_ptr->stop();
            m_vi_ptr = nullptr;
            return false;
        }

        if(!m_venc_main_ptr->start()
                || !m_venc_sub_ptr->prepare())
        {
            DEV_WRITE_LOG_ERROR("venc start failed");
            m_venc_main_ptr->stop();
            m_venc_sub_ptr->stop();
            m_venc_main_ptr = nullptr;
            m_venc_sub_ptr = nullptr;

            m_vi_ptr->stop();
            m_vi_ptr = nullptr;
            return false;
        }

        if(time_osd.enable)
        {
            m_time_osd = std::make_shared<osd_date>(
                time_osd.x,time_osd.y,time_osd.font_size,
                m_venc_sub_ptr->venc_chn());
            if(!m_time_osd->start())
            {
                DEV_WRITE_LOG_ERROR("time osd start failed");
                m_time_osd.reset();
                m_venc_main_ptr->stop();
                m_venc_sub_ptr->stop();
                m_vi_ptr->stop();
                m_venc_main_ptr.reset();
                m_venc_sub_ptr.reset();
                m_vi_ptr.reset();
                return false;
            }
        }

        m_venc_main_ptr->register_stream_observer(shared_from_this());
        m_venc_sub_ptr->register_stream_observer(shared_from_this());

        g_chns[m_chn] = shared_from_this();
        m_sub_stream_running = false;
        m_is_start = true;
        return true;
    }

    void chn::stop()
    {
        if(!m_is_start)
        {
            return;
        }
        m_is_start = false;

        if(m_time_osd)
        {
            m_time_osd->stop();
            m_time_osd.reset();
        }

        m_venc_main_ptr->unregister_stream_observer(shared_from_this());
        m_venc_sub_ptr->unregister_stream_observer(shared_from_this());

        m_venc_main_ptr->stop();
        m_venc_sub_ptr->stop();
        m_vi_ptr->stop();

        m_venc_main_ptr = nullptr;
        m_venc_sub_ptr = nullptr;
        m_vi_ptr = nullptr;
        m_sub_stream_running = false;

        g_chns[m_chn] = nullptr;
    }

    bool chn::set_sub_stream_enabled(bool enable)
    {
        if(!m_is_start || !m_venc_sub_ptr)
        {
            return false;
        }
        if(m_sub_stream_running == enable)
        {
            return true;
        }

        const bool capture_was_running = venc::capture_running();
        if(capture_was_running)
        {
            venc::stop_capture();
        }

        bool success = true;
        if(enable)
        {
            success = m_venc_sub_ptr->resume();
            if(success)
            {
                m_venc_sub_ptr->request_i_frame();
            }
        }
        else
        {
            m_venc_sub_ptr->pause();
        }

        if(capture_was_running && !venc::start_capture())
        {
            DEV_WRITE_LOG_ERROR("restart venc capture failed");
            success = false;
        }
        if(success)
        {
            m_sub_stream_running = enable;
            DEV_WRITE_LOG_INFO("sub stream encoding %s",
                               enable ? "started" : "stopped");
        }
        return success;
    }

    void chn::start_capture(bool enable)
    {
        if(enable)
        {
            venc::start_capture();
        }
        else
        {
            venc::stop_capture();
        }
    }

    bool chn::init(int32_t flag,lane_divide_mode_t lane_mode,int32_t max_w,int32_t max_h,int32_t vi_fr,int32_t wdr_mode)
    {
        g_sys_flag = flag;
        g_vi_max_w = max_w;
        g_vi_max_h = max_h;
        g_vi_fr = vi_fr;
        g_vi_wdr_mode = wdr_mode;

        if(!sys::init(flag,max_w,max_h))
        {
            return false;
        }

        vi::init();

        vi_isp::init_hs_mode(lane_mode);

        venc::init();

        return true;
    }

    void chn::release()
    {
        aplay::instance().stop();
        venc::release();
        vi::release();
        sys::release();
    }

    void chn::on_stream_come(zero_ipc::util::stream_obj_ptr sobj,zero_ipc::util::stream_head* head, const char* buf, int32_t len)
    {
        (void)sobj;
        (void)head;
        (void)buf;
        (void)len;
    }

    void chn::on_stream_error(zero_ipc::util::stream_obj_ptr sobj,int32_t errno)
    {
    }

    bool chn::is_start()
    {
        return m_is_start;
    }

    std::shared_ptr<vi> chn::video_input() const
    {
        return m_vi_ptr;
    }

    ot_venc_chn chn::svc_venc_chn() const
    {
        return m_venc_main_ptr && m_venc_main_ptr->svc_enable()
            ? m_venc_main_ptr->venc_chn()
            : OT_INVALID_HANDLE;
    }

    bool chn::request_i_frame(int32_t chn,int32_t stream)
    {
        if(chn < 0 || chn >= MAX_CHANNEL)
        {
            DEV_WRITE_LOG_ERROR("invalid channel=%d", chn);
            return false;
        }
        if(stream != MAIN_STREAM_ID && stream != SUB_STREAM_ID &&
           stream != AI_STREAM_ID)
        {
            DEV_WRITE_LOG_ERROR("invalid stream=%d", stream);
            return false;
        }

        if(stream == AI_STREAM_ID)
        {
            // The AI stream is controlled by its lifecycle worker.
            return true;
        }

        std::shared_ptr<dev::chn> chn_ptr = g_chns[chn];
        if(!chn_ptr)
        {
            return false;
        }

        if(stream == MAIN_STREAM_ID)
        {
            return chn_ptr->m_venc_main_ptr &&
                   chn_ptr->m_venc_main_ptr->request_i_frame();
        }
        if(chn_ptr->m_sub_stream_running && chn_ptr->m_venc_sub_ptr)
        {
            return chn_ptr->m_venc_sub_ptr->request_i_frame();
        }
        return false;
    }

    bool chn::enable_audio(bool enable,bool is_mic,const char* name)
    {
        g_aenc_enable = enable;
        if(g_aenc_enable)
        {
            g_aenc_name = name;
        }

        return venc::enable_audio(enable,is_mic,name);
    }

    bool chn::enable_audio_play(bool enable)
    {
        if (enable) {
            return aplay::instance().start();
        }
        aplay::instance().stop();
        return true;
    }

    bool chn::play_g711u(const uint8_t* data, size_t len)
    {
        return aplay::instance().send_g711u(data, len);
    }

}}//namespace


