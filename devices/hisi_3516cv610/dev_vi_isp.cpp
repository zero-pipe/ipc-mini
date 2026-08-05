#include "dev_vi_isp.h"
#include "dev_log.h"

#define MIPI_DEV_NAME "/dev/ot_mipi_rx"

namespace hisilicon{namespace dev{

    static combo_dev_attr_t g_dev_mipi_attr = {
        .devno = 0,
        .input_mode = INPUT_MODE_MIPI,
        .data_rate  = MIPI_DATA_RATE_X1,
        .img_rect   = {0, 0, 2688, 1520},
        .mipi_attr  = {
            DATA_TYPE_RAW_12BIT,
            OT_MIPI_WDR_MODE_NONE,
            {0, 1, 2, 3}
        }
    };

    static ot_vi_dev_attr g_mipi_raw_dev_attr = {
        .intf_mode = OT_VI_INTF_MODE_MIPI,

        /* Invalid argument */
        .work_mode = OT_VI_WORK_MODE_MULTIPLEX_1,

        /* mask component */
        .component_mask = {0xfff00000, 0x00000000},

        .scan_mode = OT_VI_SCAN_PROGRESSIVE,

        /* Invalid argument */
        .ad_chn_id = {-1, -1, -1, -1},

        /* data seq */
        .data_seq = OT_VI_DATA_SEQ_YVYU,

        /* sync param */
        .sync_cfg = {
            .vsync           = OT_VI_VSYNC_FIELD,
            .vsync_neg       = OT_VI_VSYNC_NEG_HIGH,
            .hsync           = OT_VI_HSYNC_VALID_SIG,
            .hsync_neg       = OT_VI_HSYNC_NEG_HIGH,
            .vsync_valid     = OT_VI_VSYNC_VALID_SIG,
            .vsync_valid_neg = OT_VI_VSYNC_VALID_NEG_HIGH,
            .timing_blank    = {
                /* hsync_hfb      hsync_act     hsync_hhb */
                0,                0,            0,
                /* vsync0_vhb     vsync0_act    vsync0_hhb */
                0,                0,            0,
                /* vsync1_vhb     vsync1_act    vsync1_hhb */
                0,                0,            0
            }
        },

        /* data type */
        .data_type = OT_VI_DATA_TYPE_RAW,

        /* data reverse */
        .data_reverse = TD_FALSE,

        /* input size */
        .in_size = {3840, 2160},

        /* data rate */
        .data_rate = OT_DATA_RATE_X1,
    };

    static ot_isp_pub_attr g_isp_pub_attr = {
        {0, 0, 2688, 1520},
        {2688, 1520},
        30,
        OT_ISP_BAYER_RGGB,
        OT_WDR_MODE_NONE,
        0,
        TD_FALSE,
        TD_FALSE,
        {
            TD_FALSE,
            {0, 0, 2688, 1520},
        },
    };

    vi_isp::vi_isp(int32_t w,
            int32_t h,
            int32_t src_fr,
            int32_t vi_dev,
            int32_t mipi_dev,
            int32_t sns_clk_src,
            int32_t wdr_mode,
            ot_isp_sns_obj* sns_obj,
            int32_t i2c_dev,
            int32_t flag)
        :vi(w,h,src_fr,vi_dev),m_mipi_dev(mipi_dev),m_sns_clk_src(sns_clk_src),m_wdr_mode(wdr_mode),m_sns_obj(sns_obj),m_i2c_dev(i2c_dev)
    {
        m_ldc_enable = (flag & SYS_FUNCTION_LDC_ENABLED) ? true : false;
        m_debreath_effect_enable = (flag & SYS_FUNCTION_DEBREATH_EFFECT_ENABLED) ? true : false;
        m_wrap_enable = (!m_ldc_enable) && (!m_debreath_effect_enable) && (w <= 3200);

        //wdr use 2 pipes
        if(m_wdr_mode)
        {
            m_pipes.push_back(1);
        }

        //init mipi attr
        memcpy(&m_mipi_attr,&g_dev_mipi_attr,sizeof(m_mipi_attr));
        m_mipi_attr.devno = m_mipi_dev;
        m_mipi_attr.img_rect.width = m_w;
        m_mipi_attr.img_rect.height = m_h;

        //init vi dev attr
        memcpy(&m_vi_dev_attr,&g_mipi_raw_dev_attr, sizeof(ot_vi_dev_attr));
        m_vi_dev_attr.in_size.width = m_w;
        m_vi_dev_attr.in_size.height = m_h;

        //init grp fusion attr
        memset(&m_fusion_grp_attr,0,sizeof(m_fusion_grp_attr));
        m_fusion_grp_attr.wdr_mode = (m_wdr_mode == 0) ? OT_WDR_MODE_NONE :OT_WDR_MODE_2To1_LINE;
        m_fusion_grp_attr.cache_line = m_h;
        for(td_u32 i = 0; i < m_pipes.size(); i++)
        {
            m_fusion_grp_attr.pipe_id[i] = m_pipes[i];
        }

        //init isp pub attr
        memcpy(&m_isp_pub_attr,&g_isp_pub_attr,sizeof(ot_isp_pub_attr));
        m_isp_pub_attr.wnd_rect.width = m_w;
        m_isp_pub_attr.wnd_rect.height = m_h;
        m_isp_pub_attr.sns_size.width = m_w;
        m_isp_pub_attr.sns_size.height = m_h;

        //init vi pipe attr
        memset(&m_vi_pipe_attr,0,sizeof(m_vi_pipe_attr));
        m_vi_pipe_attr.pipe_bypass_mode = OT_VI_PIPE_BYPASS_NONE;
        m_vi_pipe_attr.isp_bypass = TD_FALSE;
        m_vi_pipe_attr.size.width = m_w;
        m_vi_pipe_attr.size.height = m_h;
        m_vi_pipe_attr.pixel_format = OT_PIXEL_FORMAT_RGB_BAYER_12BPP;
        m_vi_pipe_attr.compress_mode = OT_COMPRESS_MODE_LINE;
        m_vi_pipe_attr.frame_rate_ctrl.src_frame_rate = -1;
        m_vi_pipe_attr.frame_rate_ctrl.dst_frame_rate = -1;

        //init vi chn attr
        memset(&m_vi_chn_attr,0,sizeof(m_vi_chn_attr));
        m_vi_chn_attr.size.width = m_w;
        m_vi_chn_attr.size.height = m_h;
        m_vi_chn_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        m_vi_chn_attr.dynamic_range = OT_DYNAMIC_RANGE_SDR8;
        m_vi_chn_attr.video_format = OT_VIDEO_FORMAT_LINEAR;
        m_vi_chn_attr.compress_mode = OT_COMPRESS_MODE_NONE;
        m_vi_chn_attr.mirror_en = TD_FALSE;
        m_vi_chn_attr.flip_en = TD_FALSE;
        m_vi_chn_attr.depth = 0;
        m_vi_chn_attr.frame_rate_ctrl.src_frame_rate = -1;
        m_vi_chn_attr.frame_rate_ctrl.dst_frame_rate = -1;

        //init vpss grp attr
        memset(&m_vpss_grp_attr,0,sizeof(m_vpss_grp_attr));
        m_vpss_grp_attr.ie_en                     = TD_FALSE;
        m_vpss_grp_attr.dci_en                    = TD_FALSE;
        m_vpss_grp_attr.buf_share_en              = TD_FALSE;
        m_vpss_grp_attr.mcf_en                    = TD_FALSE;
        m_vpss_grp_attr.max_width                 = m_w;
        m_vpss_grp_attr.max_height                = m_h;
        m_vpss_grp_attr.max_dei_width             = 0;
        m_vpss_grp_attr.max_dei_height            = 0;
        m_vpss_grp_attr.dynamic_range             = OT_DYNAMIC_RANGE_SDR8;
        m_vpss_grp_attr.pixel_format              = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        m_vpss_grp_attr.dei_mode                  = OT_VPSS_DEI_MODE_OFF;
        m_vpss_grp_attr.buf_share_chn             = OT_VPSS_CHN0;
        m_vpss_grp_attr.frame_rate.src_frame_rate = -1;
        m_vpss_grp_attr.frame_rate.dst_frame_rate = -1;

        //init vpss chn attr
        memset(&m_vpss_chn0_attr,0,sizeof(m_vpss_chn0_attr));
        m_vpss_chn0_attr.mirror_en                 = TD_FALSE;
        m_vpss_chn0_attr.flip_en                   = TD_FALSE;
        m_vpss_chn0_attr.border_en                 = TD_FALSE;
        m_vpss_chn0_attr.width                     = m_w;
        m_vpss_chn0_attr.height                    = m_h;
        m_vpss_chn0_attr.depth                     = 0;
        m_vpss_chn0_attr.chn_mode                  = OT_VPSS_CHN_MODE_USER;
        m_vpss_chn0_attr.video_format              = OT_VIDEO_FORMAT_LINEAR;
        m_vpss_chn0_attr.dynamic_range             = OT_DYNAMIC_RANGE_SDR8;
        m_vpss_chn0_attr.pixel_format              = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        m_vpss_chn0_attr.compress_mode             = m_ldc_enable ? OT_COMPRESS_MODE_NONE : OT_COMPRESS_MODE_SEG_COMPACT;
        m_vpss_chn0_attr.aspect_ratio.mode         = OT_ASPECT_RATIO_NONE;
        m_vpss_chn0_attr.frame_rate.src_frame_rate = -1;
        m_vpss_chn0_attr.frame_rate.dst_frame_rate = -1;

        //init 3dnr attr
        m_nr_attr.enable = TD_TRUE;
        m_nr_attr.compress_mode = OT_COMPRESS_MODE_FRAME;
        m_nr_attr.nr_type = OT_NR_TYPE_VIDEO_NORM;
        m_nr_attr.nr_motion_mode = OT_NR_MOTION_MODE_NORM;

        //init interrupt attr
        m_frame_interrupt_attr.interrupt_type = OT_FRAME_INTERRUPT_START;
        m_frame_interrupt_attr.early_line = m_vi_pipe_attr.size.height - 100 ;
    }

    vi_isp::~vi_isp()
    {
    }

    int32_t vi_isp::wdr_mode()
    {
        return m_wdr_mode;
    }

    bool vi_isp::init_hs_mode(lane_divide_mode_t mode)
    {
        td_s32 fd = open(MIPI_DEV_NAME, O_RDWR);

        if(fd < 0)
        {
            DEV_WRITE_LOG_ERROR("open mipi file:%s failed!",MIPI_DEV_NAME);
            return false;
        }

        ioctl(fd, OT_MIPI_SET_HS_MODE, &mode);

        close(fd);
        return true;
    }

    bool vi_isp::start_mipi()
    {
        td_s32 fd = open(MIPI_DEV_NAME, O_RDWR);

        if(fd < 0)
        {
            DEV_WRITE_LOG_ERROR("open mipi file:%s failed!",MIPI_DEV_NAME);
            return false;
        }

        ioctl(fd, OT_MIPI_ENABLE_MIPI_CLOCK, &m_mipi_dev);
        ioctl(fd, OT_MIPI_RESET_MIPI, &m_mipi_dev);
        ioctl(fd, OT_MIPI_SET_DEV_ATTR, &m_mipi_attr);
        ioctl(fd, OT_MIPI_UNRESET_MIPI, &m_mipi_dev);

        close(fd);
        return true;
    }

    bool vi_isp::stop_mipi()
    {
        td_s32 fd = open(MIPI_DEV_NAME, O_RDWR);
        if (fd < 0)
        {
            DEV_WRITE_LOG_ERROR("open mipi file:%s failed!",MIPI_DEV_NAME);
            return false;
        }

        ioctl(fd, OT_MIPI_RESET_MIPI, &m_mipi_dev);
        ioctl(fd, OT_MIPI_DISABLE_MIPI_CLOCK, &m_mipi_dev);

        close(fd);
        return true;
    }

    bool vi_isp::reset_sns()
    {
        td_s32 fd = open(MIPI_DEV_NAME, O_RDWR);
        if (fd < 0)
        {
            DEV_WRITE_LOG_ERROR("open mipi file:%s failed!",MIPI_DEV_NAME);
            return false;
        }

        ioctl(fd, OT_MIPI_ENABLE_SENSOR_CLOCK, &m_sns_clk_src);
        ioctl(fd, OT_MIPI_RESET_SENSOR, &m_sns_clk_src);
        ioctl(fd, OT_MIPI_UNRESET_SENSOR, &m_sns_clk_src);

        close(fd);
        return true;
    }


    void vi_isp::on_isp_proc()
    {
        ss_mpi_isp_run(m_pipes[0]);
    }

    void vi_isp::stop_isp()
    {
        ot_vi_pipe vi_pipe = m_pipes[0];
        ot_isp_3a_alg_lib ae_lib;
        ot_isp_3a_alg_lib awb_lib;

        ae_lib.id  = vi_pipe;
        awb_lib.id = vi_pipe;
        strncpy_s(ae_lib.lib_name, sizeof(ae_lib.lib_name), OT_AE_LIB_NAME, sizeof(OT_AE_LIB_NAME));
        strncpy_s(awb_lib.lib_name, sizeof(awb_lib.lib_name), OT_AWB_LIB_NAME, sizeof(OT_AWB_LIB_NAME));

        ss_mpi_isp_exit(vi_pipe);
        m_isp_thread.join();

        ss_mpi_awb_unregister(vi_pipe, &awb_lib);
        ss_mpi_ae_unregister(vi_pipe, &ae_lib);
        m_sns_obj->pfn_un_register_callback(vi_pipe, &ae_lib, &awb_lib);
    }

    bool vi_isp::start_isp()
    {
        ot_vi_pipe vi_pipe = m_pipes[0];
        ot_isp_3a_alg_lib ae_lib;
        ot_isp_3a_alg_lib awb_lib;

        ae_lib.id  = vi_pipe;
        awb_lib.id = vi_pipe;
        strncpy_s(ae_lib.lib_name, sizeof(ae_lib.lib_name), OT_AE_LIB_NAME, sizeof(OT_AE_LIB_NAME));
        strncpy_s(awb_lib.lib_name, sizeof(awb_lib.lib_name), OT_AWB_LIB_NAME, sizeof(OT_AWB_LIB_NAME));
        m_sns_obj->pfn_register_callback(vi_pipe, &ae_lib, &awb_lib);

        ot_isp_sns_commbus sns_bus_info;
        sns_bus_info.i2c_dev = m_i2c_dev;
        m_sns_obj->pfn_set_bus_info(vi_pipe, sns_bus_info);

        ss_mpi_ae_register(vi_pipe, &ae_lib);
        ss_mpi_awb_register(vi_pipe, &awb_lib);
        ss_mpi_isp_mem_init(vi_pipe);
        ss_mpi_isp_set_pub_attr(vi_pipe, &m_isp_pub_attr);
        ss_mpi_isp_init(vi_pipe);

        m_isp_thread = std::thread(std::bind(&vi_isp::on_isp_proc,this));

        return true;
    }

    bool vi_isp::start()
    {
        td_s32 ret;

        if(m_is_start)
        {
            return false;
        }

        if(!start_mipi()
                || !reset_sns())
        {
            DEV_WRITE_LOG_ERROR("start mpip failed!");
            return false;
        }

        //enable dev
        ret = ss_mpi_vi_set_dev_attr(m_vi_dev, &m_vi_dev_attr);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vi_set_dev_attr failed with %#x!", ret);
            return false;
        }

        ret = ss_mpi_vi_enable_dev(m_vi_dev);
        if (ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vi_enable_dev failed with %#x!", ret);
            return false;
        }

        //bind pipe dev
        for(td_u32 i = 0; i < m_pipes.size(); i++)
        {
            ret = ss_mpi_vi_bind(m_vi_dev,m_pipes[i]);
            if(ret != TD_SUCCESS)
            {
                DEV_WRITE_LOG_ERROR("ss_mpi_vi_bind failed with %#x!", ret);
                return false;
            }
        }

        //set fusion grp
        ot_vi_grp fusion_grp = 0;
        ret = ss_mpi_vi_set_wdr_fusion_grp_attr(fusion_grp, &m_fusion_grp_attr);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vi_set_wdr_fusion_grp_attr failed with %#x!", ret);
            return false;
        }

        //start vi pipe
        for(td_u32 i = 0; i < m_pipes.size(); i++)
        {
            int is_master_pipe = (m_pipes[i] == 0);

            ret = ss_mpi_vi_create_pipe(m_pipes[i],&m_vi_pipe_attr);
            if(ret != TD_SUCCESS)
            {
                DEV_WRITE_LOG_ERROR("ss_mpi_vi_create_pipe failed with %#x!", ret);
                return false;
            }

            if(is_master_pipe)
            {
                ret = ss_mpi_vi_set_pipe_frame_interrupt_attr(m_pipes[i], &m_frame_interrupt_attr);
                if(ret != TD_SUCCESS)
                {
                    DEV_WRITE_LOG_ERROR("ss_mpi_vi_set_pipe_frame_interattr failed with %#x!", ret);
                }
            }

            ret = ss_mpi_vi_start_pipe(m_pipes[i]);
            if(ret != TD_SUCCESS)
            {
                DEV_WRITE_LOG_ERROR("ss_mpi_vi_start_pipe failed with %#x!", ret);
                return false;
            }

            //wdr模式下，只需要启用pipe0
            if(is_master_pipe)
            {
                ret = ss_mpi_vi_set_chn_attr(m_pipes[i],m_vi_chn,&m_vi_chn_attr);
                if(ret != TD_SUCCESS)
                {
                    DEV_WRITE_LOG_ERROR("ss_mpi_vi_set_chn_attr failed with %#x!", ret);
                    return false;
                }

                ret = ss_mpi_vi_enable_chn(m_pipes[i], m_vi_chn);
                if(ret != TD_SUCCESS)
                {
                    DEV_WRITE_LOG_ERROR("ss_mpi_vi_enable_chn failed with %#x!", ret);
                    return false;
                }
            }
        }

        start_isp();

        //3dnr
        ot_3dnr_attr nr_attr;
        nr_attr.enable = TD_TRUE;
        nr_attr.nr_type = OT_NR_TYPE_VIDEO_NORM;
        nr_attr.compress_mode = OT_COMPRESS_MODE_FRAME;
        nr_attr.nr_motion_mode = OT_NR_MOTION_MODE_NORM;
        ret = ss_mpi_vi_set_pipe_3dnr_attr(m_pipes[0],&nr_attr);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vi_set_pipe_3dnr_attr failed with %#x!", ret);
        }

        //vi->vpss
        ot_mpp_chn src_chn;
        ot_mpp_chn dest_chn;
        src_chn.mod_id = OT_ID_VI;
        src_chn.dev_id = m_pipes[0];
        src_chn.chn_id = m_vi_chn;
        dest_chn.mod_id = OT_ID_VPSS;
        dest_chn.dev_id = m_vpss_grp;
        dest_chn.chn_id = m_vpss_chn;
        ret = ss_mpi_sys_bind(&src_chn, &dest_chn);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_sys_bind failed with %#x", ret);
            return false;
        }

        //vionlie_vpssonline 模式设置
        ot_frame_interrupt_attr frame_interrupt_attr;
        frame_interrupt_attr.interrupt_type = OT_FRAME_INTERRUPT_EARLY_END;
        frame_interrupt_attr.early_line = m_vpss_grp_attr.max_height / 2;
        ret = ss_mpi_vpss_set_grp_frame_interrupt_attr(0, &frame_interrupt_attr);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vpss_set_grp_frame_interrupt_attr failed with %#x", ret);
        }

        //start vpss
        ret = ss_mpi_vpss_create_grp(m_vpss_grp,&m_vpss_grp_attr);
        if (ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vpss_create_grp failed with %#x", ret);
            return false;
        }
        ret = ss_mpi_vpss_start_grp(m_vpss_grp);
        if (ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vpss_start_grp failed with %#x", ret);
            return false;
        }

        ret = ss_mpi_vpss_set_chn_attr(m_vpss_grp, m_vpss_chn, &m_vpss_chn0_attr);
        if (ret != TD_SUCCESS) 
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vpss_set_chn_attr failed with %#x", ret);
            return false;
        }

        if(m_wrap_enable)
        {
            td_u32 buf_line = 256;
            ot_pic_buf_attr buf_attr;
            td_u32 wrap_buf_size = 0;
            buf_attr.width = m_vpss_chn0_attr.width;
            buf_attr.height = m_vpss_chn0_attr.height;
            buf_attr.bit_width = OT_DATA_BIT_WIDTH_8;
            buf_attr.pixel_format = m_vpss_chn0_attr.pixel_format;
            buf_attr.compress_mode = m_vpss_chn0_attr.compress_mode;
            buf_attr.align = OT_DEFAULT_ALIGN;
            buf_attr.video_format = m_vpss_chn0_attr.video_format;
            wrap_buf_size = ot_comm_get_vpss_venc_wrap_buf_size(&buf_attr, buf_line);

            ot_vpss_chn_buf_wrap_attr wrap_attr;
            wrap_attr.enable = TD_TRUE;
            wrap_attr.buf_line = buf_line;
            wrap_attr.buf_size = wrap_buf_size;
            DEV_WRITE_LOG_INFO("WRAP INFO:enalbe=%d,buf_line:%d,buf_size:%d",wrap_attr.enable,wrap_attr.buf_line,wrap_attr.buf_size);
            ret = ss_mpi_vpss_set_chn_buf_wrap(m_vpss_grp,m_vpss_chn,&wrap_attr);
            if(ret != TD_SUCCESS)
            {
                DEV_WRITE_LOG_ERROR("ss_mpi_vpss_set_chn_buf_wrap failed with %#x", ret);
            }
        }

        ret = ss_mpi_vpss_enable_chn(m_vpss_grp, m_vpss_chn);
        if (ret != TD_SUCCESS) 
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vpss_enable_chn_attr failed with %#x", ret);
            return false;
        }

        m_is_start = true;
        return true;
    }

    void vi_isp::stop()
    {
        stop_isp();
        stop_mipi();

        ot_mpp_chn src_chn;
        ot_mpp_chn dest_chn;
        src_chn.mod_id = OT_ID_VI;
        src_chn.dev_id = m_pipes[0];
        src_chn.chn_id = m_vi_chn;
        dest_chn.mod_id = OT_ID_VPSS;
        dest_chn.dev_id = m_vpss_grp;
        dest_chn.chn_id = m_vpss_chn;
        ss_mpi_sys_unbind(&src_chn, &dest_chn);

        ss_mpi_vpss_disable_chn(m_vpss_grp, m_vpss_chn);
        ss_mpi_vpss_disable_chn(m_vpss_grp, 1);

        ss_mpi_vpss_stop_grp(m_vpss_grp);
        ss_mpi_vpss_destroy_grp(m_vpss_grp);

        for(td_u32 i = 0; i < m_pipes.size(); i++)
        {
            int is_master_pipe = (m_pipes[i] == 0);
            if(is_master_pipe)
            {
                ss_mpi_vi_disable_chn(m_pipes[i], m_vi_chn);
            }
            ss_mpi_vi_stop_pipe(m_pipes[i]);
            ss_mpi_vi_destroy_pipe(m_pipes[i]);
            ss_mpi_vi_unbind(m_vi_dev,m_pipes[i]);
        }

        ss_mpi_vi_disable_dev(m_vi_dev);
        m_is_start = false;
    }

    bool vi_isp::get_isp_exposure_info(isp_exposure_t* val)
    {
        td_s32 ret;
        ot_vi_pipe vi_pipe= m_pipes[0];

        ot_isp_exp_info info;
        ret = ss_mpi_isp_query_exposure_info(vi_pipe,&info);

        ot_isp_exposure_attr expo_attr;
        memset(&expo_attr,0,sizeof(expo_attr));
        ret = ss_mpi_isp_get_exposure_attr(vi_pipe,&expo_attr);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_isp_get_exposure_attr failed with %#x",ret);
            return false;
        }

        val->exp_time = info.exp_time;
        val->again = info.a_gain>> 10;
        val->dgain = info.d_gain>> 10;
        val->ispgain = info.isp_d_gain>> 10;
        val->iso = info.iso;
        val->is_max_exposure = info.exposure_is_max;
        val->is_exposure_stable = abs(info.hist_error) <= expo_attr.auto_attr.tolerance ? 1 : 0;

        return true;
    }

    ot_vi_pipe_attr& vi_isp::vi_pipe_attr()
    {
        return m_vi_pipe_attr;
    }

    ot_vi_chn_attr vi_isp::vi_chn_attr()
    {
        return m_vi_chn_attr;
    }

    ot_isp_pub_attr& vi_isp::isp_pub_attr()
    {
        return m_isp_pub_attr;
    }

    ot_vpss_grp_attr vi_isp::vpss_grp_attr()
    {
        return m_vpss_grp_attr;
    }

    ot_vpss_chn_attr& vi_isp::vpss_chn_attr()
    {
        return m_vpss_chn0_attr;
    }

    ot_isp_sns_obj* vi_isp::sns_obj()
    {
        return m_sns_obj;
    }

    int32_t vi_isp::isp_w()
    {
        return m_vi_pipe_attr.size.width;
    }

    int32_t vi_isp::isp_h()
    {
        return m_vi_pipe_attr.size.height;
    }

    ot_frame_interrupt_attr& vi_isp::frame_interrupt_attr()
    {
        return m_frame_interrupt_attr;
    }

}}//namespace

