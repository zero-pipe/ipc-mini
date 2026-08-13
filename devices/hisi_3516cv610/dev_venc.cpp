#include "dev_sys.h"
#include "dev_venc.h"
#include "dev_log.h"

static const uint32_t H264_MAX_VENC_BUF_SIZE =  (2 * 1024 * 1024);
static const uint32_t H265_MAX_VENC_BUF_SIZE =  (2 * 1024 * 1024);

namespace hisilicon{namespace dev{

    std::atomic_bool venc::g_is_capturing{false};
    aenc_ptr venc::g_aenc_ptr;
    std::thread venc::g_capture_thread;
    std::list<venc_ptr> venc::g_vencs;

    venc::venc(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t svc_enable,ot_venc_chn src_venc_chn)
        :stream_obj("venc_stream",chn,stream),m_venc_w(w),m_venc_h(h),m_src_fr(src_fr),m_venc_fr(venc_fr),m_vpss_grp(vpss_grp),m_vpss_chn(vpss_chn),m_venc_fd(-1),m_svc_enable(svc_enable),m_src_venc_chn(src_venc_chn)
    {
        m_venc_chn = sys::alloc_venc_chn();
    }

    venc::~venc()
    {
        if(m_venc_chn >= 0)
        {
            sys::free_venc_chn(m_venc_chn);
            m_venc_chn = -1;
        }
    }

    bool venc::init()
    {
        //for audio
        td_s32 ret;

        ss_mpi_audio_exit();
        ret = ss_mpi_audio_init();
        if (ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_audio init faild with%#x!", ret);
            return false;
        }

        ss_mpi_aenc_aac_init();

        ot_venc_mod_param venc_mod_param;

        // jpeg: enable mini buf size mode
        memset(&venc_mod_param,0,sizeof(venc_mod_param));
        venc_mod_param.mod_type = OT_VENC_MOD_JPEG;
        ret = ss_mpi_venc_get_mod_param(&venc_mod_param);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_get_mod_param faild with%#x!", ret);
            return false;
        }
        venc_mod_param.jpeg_mod_param.mini_buf_mode = 1;
        ret = ss_mpi_venc_set_mod_param(&venc_mod_param);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_set_mod_param faild with%#x!", ret);
            return false;
        }

        // h264: enable mini buf size mode
        memset(&venc_mod_param,0,sizeof(venc_mod_param));
        venc_mod_param.mod_type = OT_VENC_MOD_H264;
        ret = ss_mpi_venc_get_mod_param(&venc_mod_param);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_get_mod_param faild with%#x!", ret);
            return false;
        }
        venc_mod_param.h264_mod_param.mini_buf_mode = 1;
        ret = ss_mpi_venc_set_mod_param(&venc_mod_param);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_set_mod_param faild with%#x!", ret);
            return false;
        }

        // h265: enable mini buf size mode
        memset(&venc_mod_param,0,sizeof(venc_mod_param));
        venc_mod_param.mod_type = OT_VENC_MOD_H265;
        ret = ss_mpi_venc_get_mod_param(&venc_mod_param);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_get_mod_param faild with%#x!", ret);
            return false;
        }
        venc_mod_param.h265_mod_param.mini_buf_mode = 1;
        ret = ss_mpi_venc_set_mod_param(&venc_mod_param);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_set_mod_param faild with%#x!", ret);
            return false;
        }

        return true;
    }

    void venc::release()
    {
        ss_mpi_aenc_aac_deinit();
        ss_mpi_audio_exit();
    }

    bool venc::request_i_frame()
    {
        td_s32 ret = ss_mpi_venc_request_idr(m_venc_chn,TD_TRUE);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_request_id faild with%#x!", ret);
            return false;
        }

        return true;
    }

    bool venc::enable_debreath_effect(bool enable,int32_t strength0,int32_t strength1)
    {
        td_s32 ret = 0;
        ot_venc_debreath_effect debreath_effect;

        ret = ss_mpi_venc_get_debreath_effect(m_venc_chn, &debreath_effect);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_get_debreath_effect with error  %#x!", ret);
            return false;
        }

        if(enable)
        {
            debreath_effect.enable = TD_TRUE;
            debreath_effect.strength0 = strength0;
            debreath_effect.strength1 = strength1;
        }
        else
        {
            debreath_effect.enable = TD_FALSE;
        }

        ret = ss_mpi_venc_set_debreath_effect(m_venc_chn, &debreath_effect);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_set_debreath_effect with error  %#x!", ret);
            return false;
        }

        return true;
    }

    bool venc::enable_intra_refresh(bool enable,int32_t mode,int32_t refresh_num,int32_t request_i_qp)
    {
        td_s32 ret = 0;
        ot_venc_intra_refresh intra_refresh;
        ret = ss_mpi_venc_get_intra_refresh(m_venc_chn, &intra_refresh);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_get_intra_refresh with error  %#x!", ret);
            return false;
        }

        if(enable)
        {
            intra_refresh.enable = TD_TRUE;
            intra_refresh.mode = (ot_venc_intra_refresh_mode)mode;
            intra_refresh.refresh_num = refresh_num;
            intra_refresh.request_i_qp = request_i_qp;
        }
        else
        {
            intra_refresh.enable = TD_FALSE;
        }

        ret = ss_mpi_venc_set_intra_refresh(m_venc_chn, &intra_refresh);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_set_intra_refresh with error  %#x!", ret);
            return false;
        }

        return true;
    }

    ot_venc_chn venc::venc_chn()
    {
        return m_venc_chn;
    }

    int32_t venc::venc_fd()
    {
        return m_venc_fd;
    }

    int32_t venc::venc_w()
    {
        return m_venc_w;
    }

    int32_t venc::venc_h()
    {
        return m_venc_h;
    }

    int32_t venc::venc_fr()
    {
        return m_venc_fr;
    }

    int32_t venc::svc_enable()
    {
        return m_svc_enable;
    }

    void venc::process_audio_stream(ot_audio_stream* pstream)
    {
        zero_ipc::util::stream_head sh;

        memset(&sh,0,sizeof(sh));
        sh.type = STREAM_AUDIO_FRAME;    
        sh.time_stamp = pstream->time_stamp / 1000;

        const char* buf = (const char*)pstream->stream;
        int32_t len = pstream->len;
        std::shared_ptr<aenc_g711u> g711u_ptr = std::dynamic_pointer_cast<aenc_g711u>(g_aenc_ptr);
        if(g711u_ptr)
        {
            /* Strip HiSilicon 4-byte G711 frame header. */
            if (len <= 4) {
                return;
            }
            buf += 4;
            len -= 4;
        }
        static std::atomic<unsigned> aenc_n{0};
        if (++aenc_n == 1) {
            DEV_WRITE_LOG_INFO("aenc started G711U len=%d", len);
        }
        post_stream_to_observer(shared_from_this(),&sh,buf,len);
    }

    void venc::on_capturing()
    {
        fd_set read_fds;
        td_s32 maxfd = 0;
        struct timeval time_val;
        td_s32 ret;
        ot_venc_stream stream;
        ot_venc_chn_status stat;
        ot_venc_chn venc_chn;
        int32_t venc_fd = -1;
        int32_t aenc_fd = -1;
        ot_audio_stream aenc_stream;
        while(g_is_capturing.load())
        {
            FD_ZERO(&read_fds);

            //video
            for(auto it = g_vencs.begin(); it != g_vencs.end(); it++)
            {
                venc_fd = (*it)->venc_fd();
                FD_SET(venc_fd, &read_fds);
                if(venc_fd > maxfd)
                {
                    maxfd = venc_fd;
                }
            }

            //audio
            if(g_aenc_ptr)
            {
                aenc_fd = g_aenc_ptr->aenc_fd();
                FD_SET(aenc_fd, &read_fds);
                if(aenc_fd > maxfd)
                {
                    maxfd = aenc_fd;
                }
            }

            time_val.tv_sec  = 0;
            time_val.tv_usec = 100000;
            ret = select(maxfd + 1, &read_fds, NULL, NULL, &time_val);
            if (ret < 0)
            {
                DEV_WRITE_LOG_ERROR("select faild with %#x!", ret);
                break;
            }
            else if (ret == 0)
            {
                continue;
            }

            //audio
            if(g_aenc_ptr
                    && FD_ISSET(aenc_fd,&read_fds))
            {
                if(g_aenc_ptr->get_stream(&aenc_stream,TD_TRUE))
                {
                    for(auto it = g_vencs.begin(); it != g_vencs.end(); it++)
                    {
                        (*it)->process_audio_stream(&aenc_stream);
                    }
                    g_aenc_ptr->release_stream(&aenc_stream);
                }
            }

            for(auto it = g_vencs.begin(); it != g_vencs.end(); it++)
            {
                venc_fd = (*it)->venc_fd();
                if(!FD_ISSET(venc_fd,&read_fds))
                {
                    continue;
                }

                venc_chn = (*it)->venc_chn();
                memset(&stream, 0, sizeof(stream));
                ret = ss_mpi_venc_query_status(venc_chn, &stat);
                if (ret != TD_SUCCESS)
                {
                    DEV_WRITE_LOG_ERROR("ss_mpi_venc_query_status failed with %#x", ret);
                    continue;
                }
                if(stat.cur_packs == 0)
                {
                    DEV_WRITE_LOG_ERROR("NOTE: Current  frame is NULL!");
                    continue;
                }

                stream.pack = (ot_venc_pack*)malloc(sizeof(ot_venc_pack) * stat.cur_packs);
                if (stream.pack == NULL)
                {
                    DEV_WRITE_LOG_ERROR("malloc memory failed!");
                    break;
                }
                stream.pack_cnt = stat.cur_packs;
                ret = ss_mpi_venc_get_stream(venc_chn, &stream, TD_TRUE);
                if (ret != TD_SUCCESS)
                {
                    free(stream.pack);
                    stream.pack = NULL;
                    DEV_WRITE_LOG_ERROR("ss_mpi_venc_get_stream failed with %#x", ret);
                    break;
                }

                (*it)->process_video_stream(&stream);

                ss_mpi_venc_release_stream(venc_chn, &stream);
                free(stream.pack);
                stream.pack = NULL;
            }
        }
        DEV_WRITE_LOG_INFO("venc thread exit");
    }

    bool venc::get_audio_info(uint8_t* pacode,uint32_t* psample_rate,uint8_t* pbit_width,uint8_t* pchn_cnt)
    {
        if(!g_aenc_ptr)
        {
            return false;
        }

        *pacode = zero_ipc::util::STREAM_AUDIO_ENCODE_NONE;
        if(std::dynamic_pointer_cast<aenc_g711u>(g_aenc_ptr))
        {
            *pacode = zero_ipc::util::STREAM_AUDIO_ENCODE_G711U;
        }
        else if(std::dynamic_pointer_cast<aenc_aac>(g_aenc_ptr))
        {
            *pacode = zero_ipc::util::STREAM_AUDIO_ENCODE_AAC;
        }

        *psample_rate = g_aenc_ptr->sample_rate();
        *pbit_width  = g_aenc_ptr->bit_width();
        *pchn_cnt = g_aenc_ptr->chn_cnt();

        return true;
    }

    bool venc::enable_audio(bool enable,bool is_mic,const char* name)
    {
        if(g_is_capturing.load())
        {
            DEV_WRITE_LOG_ERROR("please stop capturing first");
            return false;
        }

        if(g_aenc_ptr)
        {
            g_aenc_ptr->stop();
            g_aenc_ptr = nullptr;
        }

        if(enable)
        {
            if(aenc::get_instance(name,is_mic,g_aenc_ptr)
                    && g_aenc_ptr->start())
            {
                return true;
            }
            g_aenc_ptr = nullptr;
            return false;
        }
        
        return true;
    }

    bool venc::start_capture()
    {
        if(g_is_capturing.exchange(true))
        {
            return false;
        }

        g_capture_thread = std::thread(&on_capturing);
        return true;
    }

    void venc::stop_capture()
    {
        if(!g_is_capturing.exchange(false))
        {
            return;
        }

        g_capture_thread.join();
    }

    bool venc::capture_running()
    {
        return g_is_capturing.load();
    }

    bool venc::prepare()
    {
        if(m_created)
        {
            return true;
        }

        td_s32 ret;

        if(m_svc_enable)
        {
            ot_venc_chn_config chn_config;
            ret = ss_mpi_venc_get_chn_config(m_venc_chn,&chn_config);
            if(ret != TD_SUCCESS)
            {
                DEV_WRITE_LOG_ERROR("ss_mpi_venc_get_chn_config[%d] faild with %#x!",m_venc_chn, ret);
                return false;
            }
            chn_config.svc_version = OT_VENC_SVC_V2;
            ret = ss_mpi_venc_set_chn_config(m_venc_chn,&chn_config);
            if(ret != TD_SUCCESS)
            {
                DEV_WRITE_LOG_ERROR("ss_mpi_venc_set_chn_config[%d] faild with %#x!",m_venc_chn, ret);
                return false;
            }
        }

        ret = ss_mpi_venc_create_chn(m_venc_chn,&m_venc_chn_attr);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_create_chn[%d] faild with %#x!",m_venc_chn, ret);
            return false;
        }
        m_created = true;
        m_venc_fd = ss_mpi_venc_get_fd(m_venc_chn);
        if(m_venc_fd < 0)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_get_fd[%d] failed",m_venc_chn);
            ss_mpi_venc_destroy_chn(m_venc_chn);
            m_created = false;
            return false;
        }
        return true;
    }

    bool venc::bind_source()
    {
        if(m_bound)
        {
            return true;
        }
        ot_mpp_chn src_chn;
        ot_mpp_chn dest_chn;
        if(m_src_venc_chn != -1)
        {
            src_chn.mod_id = OT_ID_VENC;
            src_chn.dev_id = 1;
            src_chn.chn_id = m_src_venc_chn;
            dest_chn.mod_id = OT_ID_VENC;
            dest_chn.dev_id = 0;
            dest_chn.chn_id = m_venc_chn;
        }
        else
        {
            src_chn.mod_id = OT_ID_VPSS;
            src_chn.dev_id = m_vpss_grp;
            src_chn.chn_id = m_vpss_chn;
            dest_chn.mod_id = OT_ID_VENC;
            dest_chn.dev_id = 0;
            dest_chn.chn_id = m_venc_chn;
        }

        td_s32 ret = ss_mpi_sys_bind(&src_chn, &dest_chn);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_sys_bind failed with %#x",ret);
            return false;
        }
        m_bound = true;
        return true;
    }

    void venc::unbind_source()
    {
        if(!m_bound)
        {
            return;
        }
        ot_mpp_chn src_chn;
        ot_mpp_chn dest_chn;
        if(m_src_venc_chn != -1)
        {
            src_chn.mod_id = OT_ID_VENC;
            src_chn.dev_id = 1;
            src_chn.chn_id = m_src_venc_chn;
            dest_chn.mod_id = OT_ID_VENC;
            dest_chn.dev_id = 0;
            dest_chn.chn_id = m_venc_chn;
        }
        else
        {
            src_chn.mod_id = OT_ID_VPSS;
            src_chn.dev_id = m_vpss_grp;
            src_chn.chn_id = m_vpss_chn;
            dest_chn.mod_id = OT_ID_VENC;
            dest_chn.dev_id = 0;
            dest_chn.chn_id = m_venc_chn;
        }
        ss_mpi_sys_unbind(&src_chn, &dest_chn);
        m_bound = false;
    }

    bool venc::resume()
    {
        if(m_running)
        {
            return true;
        }
        if(!prepare() || !bind_source())
        {
            return false;
        }
        td_s32 ret;
        ot_venc_start_param venc_start_param;
        venc_start_param.recv_pic_num = -1;
        ret = ss_mpi_venc_start_chn(m_venc_chn,&venc_start_param);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("HI_MPI_VENC_StartRecvPic faild with%#x!", ret);
            unbind_source();
            return false;
        }

        if(m_svc_enable)
        {
            ret = ss_mpi_venc_enable_svc(m_venc_chn,TD_TRUE);
            if(ret != TD_SUCCESS)
            {
                DEV_WRITE_LOG_ERROR("ss_mpi_enable_svc faild with%#x!", ret);
                ss_mpi_venc_stop_chn(m_venc_chn);
                unbind_source();
                return false;
            }
        }

        g_vencs.push_back(shared_from_this());
        m_running = true;
        return true;
    }

    bool venc::start()
    {
        return resume();
    }

    void venc::pause()
    {
        if(!m_running)
        {
            return;
        }
        for (auto it = g_vencs.begin();it != g_vencs.end(); it++)
        {
            if((*it)->venc_chn() == m_venc_chn)
            {
                g_vencs.erase(it);
                break;
            }
        }
        unbind_source();
        ss_mpi_venc_stop_chn(m_venc_chn);
        m_running = false;
    }

    void venc::stop()
    {
        pause();
        if(m_created)
        {
            ss_mpi_venc_destroy_chn(m_venc_chn);
            m_created = false;
            m_venc_fd = -1;
        }
    }

    bool venc::running() const
    {
        return m_running;
    }

    venc_h264::venc_h264(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t svc_enable,ot_venc_chn src_venc_chn)
        :venc(chn,stream,w,h,src_fr,venc_fr,vpss_grp,vpss_chn,svc_enable,src_venc_chn)
    {
        memset(&m_venc_chn_attr,0,sizeof(m_venc_chn_attr));
        m_venc_chn_attr.venc_attr.type = OT_PT_H264;
        m_venc_chn_attr.venc_attr.max_pic_width = m_venc_w;
        m_venc_chn_attr.venc_attr.max_pic_height = m_venc_h;
        m_venc_chn_attr.venc_attr.pic_width = m_venc_w;/*the picture width*/
        m_venc_chn_attr.venc_attr.pic_height    = m_venc_h;/*the picture height*/
        m_venc_chn_attr.venc_attr.buf_size      = std::min((uint32_t)m_venc_w * m_venc_h  * 3 / 4,H264_MAX_VENC_BUF_SIZE);/*stream buffer size*/
        m_venc_chn_attr.venc_attr.is_by_frame      = TD_TRUE;/*get stream mode is slice mode or frame mode?*/
        m_venc_chn_attr.venc_attr.profile = 0;
        m_venc_chn_attr.venc_attr.h264_attr.rcn_ref_share_buf_en = TD_TRUE;
        m_venc_chn_attr.venc_attr.h264_attr.frame_buf_ratio = 75;
        m_venc_chn_attr.gop_attr.gop_mode = OT_VENC_GOP_MODE_NORMAL_P;
        m_venc_chn_attr.gop_attr.normal_p.ip_qp_delta = 3; /* 3 is a number */
    }

    venc_h264::~venc_h264()
    {
    }

    void venc_h264::process_video_stream(ot_venc_stream* pstream)
    {
        if(!pstream || !pstream->pack || pstream->pack_cnt == 0)
        {
            return;
        }

        zero_ipc::util::stream_head sh;
        memset(&sh,0,sizeof(sh));
        sh.type = STREAM_NALU_SLICE;

        for(td_u32 i = 0; i < pstream->pack_cnt &&
                         sh.nalu_count < MAX_STREAM_NALU_COUNT; ++i)
        {
            const auto& pack = pstream->pack[i];
            if(!pack.addr || pack.offset > pack.len)
            {
                continue;
            }
            const td_u32 es_len = pack.len - pack.offset;
            if(es_len < 5)
            {
                continue;
            }
            auto* es_buf = reinterpret_cast<char*>(pack.addr + pack.offset);
            const int32_t es_type = es_buf[4] & 0x1f;
            if(es_type != 0x7 && es_type != 0x8 &&
               es_type != 0x1 && es_type != 0x5)
            {
                continue;
            }

            auto& nalu = sh.nalu[sh.nalu_count++];
            nalu.data = reinterpret_cast<uint8_t*>(es_buf);
            nalu.size = es_len;
            nalu.time_stamp = pack.pts / 1000;
        }

        if(sh.nalu_count > 0)
        {
            post_stream_to_observer(shared_from_this(),&sh,NULL,0);
        }
    }

    venc_h264_cbr::venc_h264_cbr(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t bitrate,int32_t svc_enable,ot_venc_chn src_venc_chn)
        :venc_h264(chn,stream,w,h,src_fr,venc_fr,vpss_grp,vpss_chn,svc_enable,src_venc_chn),m_bitrate(bitrate)
    {
        m_venc_chn_attr.rc_attr.rc_mode = OT_VENC_RC_MODE_H264_CBR;
        m_venc_chn_attr.rc_attr.h264_cbr.gop = m_venc_fr; /*the interval of IFrame*/
        m_venc_chn_attr.rc_attr.h264_cbr.stats_time = 1; /* stream rate statics time(s) */
        m_venc_chn_attr.rc_attr.h264_cbr.src_frame_rate= m_src_fr; /* input (vi) frame rate */
        m_venc_chn_attr.rc_attr.h264_cbr.dst_frame_rate = m_venc_fr; /* target frame rate */
        m_venc_chn_attr.rc_attr.h264_cbr.bit_rate = m_bitrate;
    }

    venc_h264_cbr::~venc_h264_cbr()
    {
    }

    venc_h264_avbr::venc_h264_avbr(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t max_bitrate,int32_t svc_enable,ot_venc_chn src_venc_chn)
        :venc_h264(chn,stream,w,h,src_fr,venc_fr,vpss_grp,vpss_chn,svc_enable,src_venc_chn),m_max_bitrate(max_bitrate)
    {
        m_venc_chn_attr.rc_attr.rc_mode = OT_VENC_RC_MODE_H264_AVBR;
        m_venc_chn_attr.rc_attr.h264_avbr.gop = m_venc_fr; /*the interval of IFrame*/
        m_venc_chn_attr.rc_attr.h264_avbr.stats_time = 1; /* stream rate statics time(s) */
        m_venc_chn_attr.rc_attr.h264_avbr.src_frame_rate= m_src_fr; /* input (vi) frame rate */
        m_venc_chn_attr.rc_attr.h264_avbr.dst_frame_rate = m_venc_fr; /* target frame rate */
        m_venc_chn_attr.rc_attr.h264_avbr.max_bit_rate = m_max_bitrate;
    }

    venc_h264_avbr::~venc_h264_avbr()
    {
    }

    venc_h265::venc_h265(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t svc_enable,ot_venc_chn src_venc_chn)
        :venc(chn,stream,w,h,src_fr,venc_fr,vpss_grp,vpss_chn,svc_enable,src_venc_chn)
    {
        memset(&m_venc_chn_attr,0,sizeof(m_venc_chn_attr));
        m_venc_chn_attr.venc_attr.type = OT_PT_H265;
        m_venc_chn_attr.venc_attr.max_pic_width = m_venc_w;
        m_venc_chn_attr.venc_attr.max_pic_height = m_venc_h;
        m_venc_chn_attr.venc_attr.pic_width = m_venc_w;/*the picture width*/
        m_venc_chn_attr.venc_attr.pic_height    = m_venc_h;/*the picture height*/
        m_venc_chn_attr.venc_attr.buf_size      = std::min((uint32_t)m_venc_w * m_venc_h  * 3 / 4,H265_MAX_VENC_BUF_SIZE);/*stream buffer size*/
        m_venc_chn_attr.venc_attr.is_by_frame      = TD_TRUE;/*get stream mode is slice mode or frame mode?*/
        m_venc_chn_attr.venc_attr.profile = 0;
        m_venc_chn_attr.venc_attr.h265_attr.rcn_ref_share_buf_en = TD_TRUE;
        m_venc_chn_attr.venc_attr.h265_attr.frame_buf_ratio = 75;
        m_venc_chn_attr.gop_attr.gop_mode = OT_VENC_GOP_MODE_NORMAL_P;
        m_venc_chn_attr.gop_attr.normal_p.ip_qp_delta = 2; /* 2 is a number */
    }

    venc_h265::~venc_h265()
    {
    }

    void venc_h265::process_video_stream(ot_venc_stream* pstream)
    {
        if(!pstream || !pstream->pack || pstream->pack_cnt == 0)
        {
            return;
        }

        zero_ipc::util::stream_head sh;
        memset(&sh,0,sizeof(sh));
        sh.type = STREAM_NALU_SLICE;

        for(td_u32 i = 0; i < pstream->pack_cnt &&
                         sh.nalu_count < MAX_STREAM_NALU_COUNT; ++i)
        {
            const auto& pack = pstream->pack[i];
            if(!pack.addr || pack.offset > pack.len)
            {
                continue;
            }
            const td_u32 es_len = pack.len - pack.offset;
            if(es_len < 5)
            {
                continue;
            }
            auto* es_buf = reinterpret_cast<char*>(pack.addr + pack.offset);
            auto& nalu = sh.nalu[sh.nalu_count++];
            nalu.data = reinterpret_cast<uint8_t*>(es_buf);
            nalu.size = es_len;
            nalu.time_stamp = pack.pts / 1000;
        }

        if(sh.nalu_count > 0)
        {
            post_stream_to_observer(shared_from_this(),&sh,NULL,0);
        }
    }

    venc_h265_cbr::venc_h265_cbr(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t bitrate,int32_t svc_enable,ot_venc_chn src_venc_chn)
        :venc_h265(chn,stream,w,h,src_fr,venc_fr,vpss_grp,vpss_chn,svc_enable,src_venc_chn),m_bitrate(bitrate)
    {
        m_venc_chn_attr.rc_attr.rc_mode = OT_VENC_RC_MODE_H265_CBR;
        m_venc_chn_attr.rc_attr.h265_cbr.gop = m_venc_fr; /*the interval of IFrame*/
        m_venc_chn_attr.rc_attr.h265_cbr.stats_time = 1; /* stream rate statics time(s) */
        m_venc_chn_attr.rc_attr.h265_cbr.src_frame_rate= m_src_fr; /* input (vi) frame rate */
        m_venc_chn_attr.rc_attr.h265_cbr.dst_frame_rate = m_venc_fr; /* target frame rate */
        m_venc_chn_attr.rc_attr.h265_cbr.bit_rate = m_bitrate;
    }

    venc_h265_cbr::~venc_h265_cbr()
    {
    }

    venc_h265_avbr::venc_h265_avbr(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t max_bitrate,int32_t svc_enable,ot_venc_chn src_venc_chn)
        :venc_h265(chn,stream,w,h,src_fr,venc_fr,vpss_grp,vpss_chn,svc_enable,src_venc_chn),m_max_bitrate(max_bitrate)
    {
        m_venc_chn_attr.rc_attr.rc_mode = OT_VENC_RC_MODE_H265_AVBR;
        m_venc_chn_attr.rc_attr.h265_avbr.gop = m_venc_fr; /*the interval of IFrame*/
        m_venc_chn_attr.rc_attr.h265_avbr.stats_time = 1; /* stream rate statics time(s) */
        m_venc_chn_attr.rc_attr.h265_avbr.src_frame_rate= m_src_fr; /* input (vi) frame rate */
        m_venc_chn_attr.rc_attr.h265_avbr.dst_frame_rate = m_venc_fr; /* target frame rate */
        m_venc_chn_attr.rc_attr.h265_avbr.max_bit_rate = m_max_bitrate;
    }

    venc_h265_avbr::~venc_h265_avbr()
    {
    }

    venc_jpg::venc_jpg(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t quality)
        :venc(chn,stream,w,h,src_fr,venc_fr,vpss_grp,vpss_chn,0,-1)
    {
        memset(&m_venc_chn_attr,0,sizeof(m_venc_chn_attr));
        m_venc_chn_attr.venc_attr.type = OT_PT_JPEG;
        m_venc_chn_attr.venc_attr.max_pic_width = m_venc_w;
        m_venc_chn_attr.venc_attr.max_pic_height = m_venc_h;
        m_venc_chn_attr.venc_attr.pic_width = m_venc_w;/*the picture width*/
        m_venc_chn_attr.venc_attr.pic_height    = m_venc_h;/*the picture height*/
        m_venc_chn_attr.venc_attr.buf_size      = m_venc_w * m_venc_h  * 3 / 4;/*stream buffer size*/
        m_venc_chn_attr.venc_attr.is_by_frame      = TD_TRUE;/*get stream mode is slice mode or frame mode?*/
        m_venc_chn_attr.venc_attr.profile = 0;
        m_venc_chn_attr.venc_attr.jpeg_attr.dcf_en = TD_TRUE;
        m_venc_chn_attr.venc_attr.jpeg_attr.mpf_cfg.large_thumbnail_num = 0;
        m_venc_chn_attr.venc_attr.jpeg_attr.recv_mode = OT_VENC_PIC_RECV_SINGLE;
    }

    venc_jpg::~venc_jpg()
    {
    }

    void venc_jpg::process_video_stream(ot_venc_stream* pstream)
    {
    }
}}//namespace


