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
        venc_encode_options main;
        main.width = venc_w;
        main.height = venc_h;
        main.frame_rate = fr;
        main.bitrate_kbps = bitrate;
        main.svc_enable = svc_enable;
        venc_encode_options sub;
        sub.width = 720;
        sub.height = 480;
        sub.frame_rate = fr;
        sub.bitrate_kbps = 1000;
        sub.osd = time_osd;
        return start(main, sub);
    }

    bool chn::start(const venc_encode_options& main,
                    const venc_encode_options& sub)
    {
        if(m_is_start || m_chn < 0 || m_chn >= MAX_CHANNEL || !main.enable)
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

        if(main.width > m_vi_ptr->w()
                || main.height > m_vi_ptr->h()
                || main.frame_rate > m_vi_ptr->fr())
        {
            DEV_WRITE_LOG_ERROR("invalid param");
            return false;
        }

        // With wrap buffers, VPSS channel 0 must match the main encoder size.
        std::shared_ptr<vi_isp> viisp = std::dynamic_pointer_cast<vi_isp>(m_vi_ptr);
        if(viisp)
        {
            if(viisp->vpss_chn_attr().width != (td_u32)main.width)
            {
                viisp->vpss_chn_attr().width = main.width;
            }
            if(viisp->vpss_chn_attr().height != (td_u32)main.height)
            {
                viisp->vpss_chn_attr().height = main.height;
            }
        }

        if(!m_vi_ptr->start())
        {
            DEV_WRITE_LOG_ERROR("vi start failed");
            m_vi_ptr.reset();
            return false;
        }

        auto make_venc = [this](int32_t stream_id, const venc_encode_options& opt,
                                int32_t src_fr, int32_t vpss_grp, int32_t vpss_chn,
                                int32_t bind_venc) -> std::shared_ptr<venc> {
            if(m_venc_mode == "H264_CBR")
            {
                return std::make_shared<venc_h264_cbr>(
                    m_chn, stream_id, opt.width, opt.height, src_fr, opt.frame_rate,
                    vpss_grp, vpss_chn, opt.bitrate_kbps, opt.svc_enable, bind_venc);
            }
            if(m_venc_mode == "H264_AVBR")
            {
                return std::make_shared<venc_h264_avbr>(
                    m_chn, stream_id, opt.width, opt.height, src_fr, opt.frame_rate,
                    vpss_grp, vpss_chn, opt.bitrate_kbps, opt.svc_enable, bind_venc);
            }
            if(m_venc_mode == "H265_CBR")
            {
                return std::make_shared<venc_h265_cbr>(
                    m_chn, stream_id, opt.width, opt.height, src_fr, opt.frame_rate,
                    vpss_grp, vpss_chn, opt.bitrate_kbps, opt.svc_enable, bind_venc);
            }
            if(m_venc_mode == "H265_AVBR")
            {
                return std::make_shared<venc_h265_avbr>(
                    m_chn, stream_id, opt.width, opt.height, src_fr, opt.frame_rate,
                    vpss_grp, vpss_chn, opt.bitrate_kbps, opt.svc_enable, bind_venc);
            }
            return nullptr;
        };

        m_venc_main_ptr = make_venc(MAIN_STREAM_ID, main, m_vi_ptr->fr(),
                                    m_vi_ptr->vpss_grp(), m_vi_ptr->vpss_chn(), -1);
        if(!m_venc_main_ptr)
        {
            DEV_WRITE_LOG_ERROR("invalid venc mode");
            m_vi_ptr->stop();
            m_vi_ptr = nullptr;
            return false;
        }
        if(sub.enable)
        {
            venc_encode_options sub_opt = sub;
            sub_opt.svc_enable = 0;
            m_venc_sub_ptr = make_venc(SUB_STREAM_ID, sub_opt, sub.frame_rate,
                                       -1, -1, m_venc_main_ptr->venc_chn());
        }

        if(!m_venc_main_ptr->start()
                || (m_venc_sub_ptr && !m_venc_sub_ptr->prepare()))
        {
            DEV_WRITE_LOG_ERROR("venc start failed");
            m_venc_main_ptr->stop();
            if(m_venc_sub_ptr)
            {
                m_venc_sub_ptr->stop();
            }
            m_venc_main_ptr = nullptr;
            m_venc_sub_ptr = nullptr;
            m_vi_ptr->stop();
            m_vi_ptr = nullptr;
            return false;
        }

        auto start_time_osd = [](std::shared_ptr<osd_date>& slot,
                                 const time_osd_options& osd,
                                 ot_venc_chn venc_chn,
                                 const char* name) {
            if(!osd.enable)
            {
                return true;
            }
            slot = std::make_shared<osd_date>(osd.x, osd.y, osd.font_size, venc_chn);
            if(slot->start())
            {
                return true;
            }
            DEV_WRITE_LOG_ERROR("time osd start failed on %s", name);
            slot.reset();
            return false;
        };
        if(!start_time_osd(m_time_osd_main, main.osd, m_venc_main_ptr->venc_chn(), "main")
                || (m_venc_sub_ptr
                    && !start_time_osd(m_time_osd_sub, sub.osd,
                                       m_venc_sub_ptr->venc_chn(), "sub")))
        {
            if(m_time_osd_main)
            {
                m_time_osd_main->stop();
                m_time_osd_main.reset();
            }
            if(m_time_osd_sub)
            {
                m_time_osd_sub->stop();
                m_time_osd_sub.reset();
            }
            m_venc_main_ptr->stop();
            if(m_venc_sub_ptr)
            {
                m_venc_sub_ptr->stop();
            }
            m_vi_ptr->stop();
            m_venc_main_ptr.reset();
            m_venc_sub_ptr.reset();
            m_vi_ptr.reset();
            return false;
        }

        m_venc_main_ptr->register_stream_observer(shared_from_this());
        if(m_venc_sub_ptr)
        {
            m_venc_sub_ptr->register_stream_observer(shared_from_this());
        }

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

        if(m_time_osd_main)
        {
            m_time_osd_main->stop();
            m_time_osd_main.reset();
        }
        if(m_time_osd_sub)
        {
            m_time_osd_sub->stop();
            m_time_osd_sub.reset();
        }

        if(m_venc_main_ptr)
        {
            m_venc_main_ptr->unregister_stream_observer(shared_from_this());
            m_venc_main_ptr->stop();
        }
        if(m_venc_sub_ptr)
        {
            m_venc_sub_ptr->unregister_stream_observer(shared_from_this());
            m_venc_sub_ptr->stop();
        }
        if(m_vi_ptr)
        {
            m_vi_ptr->stop();
        }

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


