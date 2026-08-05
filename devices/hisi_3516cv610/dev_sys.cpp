#include "dev_sys.h"
#include "dev_log.h"
#include <mutex>

namespace hisilicon{namespace dev{

    bool sys::init(int32_t flag,int32_t max_w,int32_t max_h)
    {
        ot_vb_cfg vb_cfg;
        ot_vb_calc_cfg calc_cfg;
        ot_pic_buf_attr buf_attr;
        td_s32 ret;
        bool aiisp_enabled = (flag & SYS_FUNCTION_AIISP_ENABLED) ? true : false;
        bool ldc_enabled = (flag & SYS_FUNCTION_LDC_ENABLED) ? true : false;
        bool debreath_effect_enabled = (flag & SYS_FUNCTION_DEBREATH_EFFECT_ENABLED) ? true : false;
        bool wrap_enabled = (!ldc_enabled) && (!debreath_effect_enabled) && (max_w <= 3200);
        bool wdr_enable = (flag & SYS_FUNCTION_WDR_ENABLE) ? true : false;

        ss_mpi_sys_exit();
        ss_mpi_vb_exit();

        if(max_w > 4096 || max_h > 4096)
        {
            DEV_WRITE_LOG_ERROR("unsupport max_w:%d,max_h:%d",max_w,max_h);
            return false;
        }

        //VI_ONLINE_VPSS_ONLINE,width限制为[120,3200],height限制为[80,4096]
        //VI_OFFLINE_VPSS_ONLINE,width限制为[120,4096],height限制为[80,4096]
        //VI_OFFLINE_VPSS_OFFLINE,width限制为[120,4096],height限制为[80,4096]
        ot_vi_vpss_mode vi_vpss_mode;
        memset(&vi_vpss_mode,0,sizeof(vi_vpss_mode));

        bool vi_offline = false;
        bool vpss_offline = false; 
        if(max_w <= 3200 && max_h <= 4096)
        {
            if(aiisp_enabled)
           {
               //aiisp开启，需要vi自己attach到user pool(详见aiisp/aiisp_bnr.cpp),需要VI_OFFLINE
               vi_offline = true;
           }
        }
        else
        {
            //4K需要设置
            vi_offline = true;
            vpss_offline = false;
        }

        if(!vi_offline && !vpss_offline)
        {
            vi_vpss_mode.mode[0] = OT_VI_ONLINE_VPSS_ONLINE;
        }
        else if(vi_offline && vpss_offline)
        {
            vi_vpss_mode.mode[0] = OT_VI_OFFLINE_VPSS_OFFLINE;
        }
        else if(!vi_offline && vpss_offline)
        {
            vi_vpss_mode.mode[0] = OT_VI_ONLINE_VPSS_OFFLINE;
        }
        else if(vi_offline && !vpss_offline)
        {
            vi_vpss_mode.mode[0] = OT_VI_OFFLINE_VPSS_ONLINE;
        }

        for(int32_t i = 1; i < OT_VI_MAX_PIPE_NUM; i++)
        {
            vi_vpss_mode.mode[i] = (vi_vpss_mode.mode[0] == OT_VI_OFFLINE_VPSS_ONLINE) ? OT_VI_OFFLINE_VPSS_ONLINE : OT_VI_OFFLINE_VPSS_OFFLINE;
        }

        memset(&vb_cfg,0,sizeof(ot_vb_cfg));

        //通过cat /proc/umap/media-mem最后一行:remain=xxx来查看可用MMZ
        /*如何确保vb分配最小化(最合理)
         * 1. cat /proc/umap/vb 
         * 2. 查看pool_type=common,owner=common,common说明这些vb快是通过ss_mpi_vb_set_cfg分配的
         * 3. 确保min_free为0 且 cat /dev/logmpp无获取不到vb报错信息
         * 满足上诉说明设置的vb是最小化的，是最合理的，这样子能让出更多的MMZ内存给其他模块
         * */

        //raw
        buf_attr.width         = max_w;
        buf_attr.height        = max_h;
        buf_attr.align         = OT_DEFAULT_ALIGN;
        buf_attr.bit_width     = OT_DATA_BIT_WIDTH_8;
        buf_attr.pixel_format  = OT_PIXEL_FORMAT_RGB_BAYER_12BPP;
        buf_attr.compress_mode = OT_COMPRESS_MODE_LINE;
        buf_attr.video_format  = OT_VIDEO_FORMAT_LINEAR;
        ot_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);
        vb_cfg.common_pool[0].blk_size = calc_cfg.vb_size;
        if(vi_offline)
        {
            //VI_OFFLINE需要设置vb块
            if(aiisp_enabled)
            {
                //aiisp开启，vi使用了user pool,详见aiisp/aiisp_bnr.cpp代码
                //所以aiisp开启的情况下,不需要设置vb块
                vb_cfg.common_pool[0].blk_cnt = 0;
            }
            else
            {
                vb_cfg.common_pool[0].blk_cnt = wdr_enable ? 6 : 3;
            }
        }
        else
        {
            //VI_ONLINE设置为0
            vb_cfg.common_pool[0].blk_cnt = 0;
        }

        //yuv
        buf_attr.width         = max_w;
        buf_attr.height        = max_h;
        buf_attr.align         = OT_DEFAULT_ALIGN;
        buf_attr.bit_width     = OT_DATA_BIT_WIDTH_8;
        buf_attr.pixel_format  = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        buf_attr.compress_mode = OT_COMPRESS_MODE_NONE;
        buf_attr.video_format  = OT_VIDEO_FORMAT_LINEAR;
        ot_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);
        vb_cfg.common_pool[1].blk_size = calc_cfg.vb_size;
        if(vpss_offline)
        {
            //VPSS_OFFLINE需要设置vb块
            vb_cfg.common_pool[1].blk_cnt = 3;
        }
        else
        {
            //VPSS_ONLINE设置为0
            //ldc,去除呼吸效应需要vb块
            vb_cfg.common_pool[1].blk_cnt = (ldc_enabled || debreath_effect_enabled) ? 3 : 0;
        }

        //venc sub
        buf_attr.width         = 720;
        buf_attr.height        = 480;
        buf_attr.align         = OT_DEFAULT_ALIGN;
        buf_attr.bit_width     = OT_DATA_BIT_WIDTH_8;
        buf_attr.pixel_format  = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        buf_attr.compress_mode = OT_COMPRESS_MODE_NONE;
        buf_attr.video_format  = OT_VIDEO_FORMAT_LINEAR;
        ot_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);
        vb_cfg.common_pool[2].blk_size = calc_cfg.vb_size;
        vb_cfg.common_pool[2].blk_cnt = 1;

        //vpss wrap
        buf_attr.width = max_w;
        buf_attr.height = max_h;
        buf_attr.bit_width = OT_DATA_BIT_WIDTH_8;
        buf_attr.pixel_format = OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420;
        buf_attr.compress_mode = OT_COMPRESS_MODE_SEG_COMPACT;
        buf_attr.align = OT_DEFAULT_ALIGN;
        buf_attr.video_format = OT_VIDEO_FORMAT_LINEAR;
        vb_cfg.common_pool[3].blk_size = ot_comm_get_vpss_venc_wrap_buf_size(&buf_attr, 256);
        if(wrap_enabled)
        {
            vb_cfg.common_pool[3].blk_cnt = 1;
        }
        else
        {
            vb_cfg.common_pool[3].blk_cnt = 0;
        }

        vb_cfg.max_pool_cnt= 4;

        ret = ss_mpi_vb_set_cfg(&vb_cfg);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vb_set_cfg failed with error 0x%x",ret);
            return false;
        }

        ot_vb_supplement_cfg vb_supplement_cfg = {0};
        vb_supplement_cfg.supplement_cfg = OT_VB_SUPPLEMENT_BNR_MOT_MASK | OT_VB_SUPPLEMENT_JPEG_MASK;
        ret = ss_mpi_vb_set_supplement_cfg(&vb_supplement_cfg);
        if (ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vb_set_supplement failed with error 0x%x",ret);
            return false;
        }

        ret = ss_mpi_vb_init();
        if (ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vb_init failed with error 0x%x",ret);
            return false;
        }

        ret = ss_mpi_sys_init();
        if (ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_sys_init failed with error 0x%x",ret);
            return false;
        }

       
        ret = ss_mpi_sys_set_vi_vpss_mode(&vi_vpss_mode);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_sys_set_vi_vpss_mode failed with error 0x%x",ret);
            return false;
        }

        ret = ss_mpi_sys_set_vi_aiisp_mode(0, OT_VI_AIISP_MODE_DEFAULT);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_sys_set_vi_aiisp_mode failed with error 0x%x",ret);
            return false;
        }

        return true;
    }

    void sys::release()
    {
        ss_mpi_sys_exit();
        ss_mpi_vb_exit_mod_common_pool(OT_VB_UID_VDEC);
        ss_mpi_vb_exit();
    }

    void sys::release_unexcepted()
    {
        /**
         * 当遇到段错误后，有些资源有可能无法清除干净，在这个函数下，把所有的资源按流程全部清干净
         */

        //clear all venc
        ot_mpp_chn src_chn;
        ot_mpp_chn dest_chn;
        for(ot_venc_chn venc_chn = 0; venc_chn < OT_VENC_MAX_CHN_NUM; venc_chn++)
        {
            //detach osd
            for(ot_rgn_handle rgn_handle = 0; rgn_handle < OT_RGN_HANDLE_MAX; rgn_handle++)
            {
                src_chn.mod_id = OT_ID_VENC;
                src_chn.dev_id = 0;
                src_chn.chn_id = venc_chn;
                ss_mpi_rgn_detach_from_chn(rgn_handle, &src_chn);
            }

            //unbind vpss
            for(ot_vpss_grp vpss_grp = 0; vpss_grp < OT_VPSS_MAX_GRP_NUM; vpss_grp++)
            {
                for(ot_vpss_chn vpss_chn = 0; vpss_chn < OT_VPSS_MAX_PHYS_CHN_NUM; vpss_chn++)
                {
                    src_chn.mod_id = OT_ID_VPSS;
                    src_chn.dev_id = vpss_grp;
                    src_chn.chn_id = vpss_chn;
                    dest_chn.mod_id = OT_ID_VENC;
                    dest_chn.dev_id = 0;
                    dest_chn.chn_id = venc_chn;
                    ss_mpi_sys_unbind(&src_chn, &dest_chn);
                }
            }

            ss_mpi_venc_stop_chn(venc_chn);
            ss_mpi_venc_destroy_chn(venc_chn);
        }

        //clear all osd
        for(ot_rgn_handle rgn_handle = 0; rgn_handle < OT_RGN_HANDLE_MAX; rgn_handle++)
        {
            ss_mpi_rgn_destroy(rgn_handle); 
        }

        //clear all vpss
        for(ot_vpss_grp vpss_grp = 0; vpss_grp < OT_VPSS_MAX_GRP_NUM; vpss_grp++)
        {
            for(ot_vpss_chn vpss_chn = 0; vpss_chn < OT_VPSS_MAX_PHYS_CHN_NUM; vpss_chn++)
            {
                for(ot_vi_pipe vi_pipe = 0; vi_pipe < OT_VI_MAX_PHYS_PIPE_NUM; vi_pipe++)
                {
                    for(ot_vi_chn vi_chn = 0; vi_chn < OT_VI_MAX_CHN_NUM; vi_chn++)
                    {
                        src_chn.mod_id = OT_ID_VI;
                        src_chn.dev_id = vi_pipe;
                        src_chn.chn_id = vi_chn;
                        dest_chn.mod_id = OT_ID_VPSS;
                        dest_chn.dev_id = vpss_grp;
                        dest_chn.chn_id = vpss_chn;
                        ss_mpi_sys_unbind(&src_chn, &dest_chn);
                    }
                }
                ss_mpi_vpss_disable_chn(vpss_grp, vpss_chn);
            }

            ss_mpi_vpss_stop_grp(vpss_grp);
            ss_mpi_vpss_destroy_grp(vpss_grp);
        }

        //clear all vi pipe
        for(ot_vi_pipe vi_pipe = 0; vi_pipe < OT_VI_MAX_PHYS_PIPE_NUM; vi_pipe++)
        {
            for(ot_vi_chn vi_chn = 0; vi_chn < OT_VI_MAX_CHN_NUM; vi_chn++)
            {
                ss_mpi_vi_disable_chn(vi_pipe, vi_chn);
            }
            ss_mpi_isp_exit(vi_pipe);
            ss_mpi_vi_stop_pipe(vi_pipe);
            ss_mpi_vi_destroy_pipe(vi_pipe);

            for(ot_vi_dev vi_dev = 0; vi_dev < OT_VI_MAX_DEV_NUM; vi_dev++)
            {
                ss_mpi_vi_unbind(vi_dev,vi_pipe);
            }
        }

        //clear all vi dev
        for(ot_vi_dev vi_dev = 0; vi_dev < OT_VI_MAX_DEV_NUM; vi_dev++)
        {
            ss_mpi_vi_disable_dev(vi_dev);
        }

        //vb release
        ss_mpi_sys_exit();
        ss_mpi_vb_exit_mod_common_pool(OT_VB_UID_VDEC);
        ss_mpi_vb_exit();
    }

    static std::mutex g_venc_chn_mu;
    static bool g_venc_inited = false;
    static bool g_venc_flag[OT_VENC_MAX_CHN_NUM];

    void sys::free_venc_chn(ot_venc_chn chn)
    {
        std::unique_lock<std::mutex> lock(g_venc_chn_mu);

        if(chn >= 0 && chn < OT_VENC_MAX_CHN_NUM)
        {
            g_venc_flag[chn] = false;
        }
    }

    ot_venc_chn sys::alloc_venc_chn()
    {
        std::unique_lock<std::mutex> lock(g_venc_chn_mu);
        if(!g_venc_inited)
        {
            g_venc_inited = true;
            for(int32_t i = 0; i < OT_VENC_MAX_CHN_NUM; i++)
            {
                g_venc_flag[i] = false;
            }
        }

        for(int32_t i = 0; i < OT_VENC_MAX_CHN_NUM; i++)
        {
            if(!g_venc_flag[i])
            {
                g_venc_flag[i] = true;
                return (ot_venc_chn)i;
            }
        }

        return -1;
    }

    static std::mutex g_rgn_mu;
    static bool g_rgn_inited = false;
    static bool g_rgn_flag[OT_RGN_HANDLE_MAX];
    void sys::free_rgn_handle(ot_rgn_handle hdl)
    {
        std::unique_lock<std::mutex> lock(g_rgn_mu);

        if(hdl < OT_RGN_HANDLE_MAX)
        {
            g_rgn_flag[hdl] = false;
        }
    }

    ot_rgn_handle sys::alloc_rgn_handle()
    {
        std::unique_lock<std::mutex> lock(g_rgn_mu);
        if(!g_rgn_inited)
        {
            g_rgn_inited = true;
            for(int32_t i = 0; i < OT_RGN_HANDLE_MAX; i++)
            {
                g_rgn_flag[i] = false;
            }
        }

        for(int32_t i = 0; i < OT_RGN_HANDLE_MAX; i++)
        {
            if(!g_rgn_flag[i])
            {
                g_rgn_flag[i] = true;
                return (ot_rgn_handle)i;
            }
        }

        return OT_INVALID_HANDLE;
    }

}}//namespace
