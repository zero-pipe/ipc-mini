#include "dev_vi_gc8613_liner.h"
#include "dev_log.h"
extern ot_isp_sns_obj g_sns_gc8613_4k20_obj;
extern ot_isp_sns_obj g_sns_gc8613_1080p20_obj;
#define GC8613_SUPPORT_MAX_W 3840
#define GC8613_SUPPORT_MAX_H 2160

static combo_dev_attr_t g_mipi_4lane_chn0_sensor_gc8613_10bit_8m_nowdr_attr = {
    .devno = 0,
    .input_mode = INPUT_MODE_MIPI,
    .data_rate = MIPI_DATA_RATE_X1,
    .img_rect = {0, 0, GC8613_SUPPORT_MAX_W, GC8613_SUPPORT_MAX_H},
    .mipi_attr = {
        DATA_TYPE_RAW_10BIT,
        OT_MIPI_WDR_MODE_NONE,
        {2, 0, -1, -1}
    }
};

static ot_isp_pub_attr g_isp_pub_attr_gc8613_mipi_8m_30fps = {
    { 0, 0, GC8613_SUPPORT_MAX_W, GC8613_SUPPORT_MAX_H},
    {GC8613_SUPPORT_MAX_W, GC8613_SUPPORT_MAX_H},
    30,
    OT_ISP_BAYER_RGGB,
    OT_WDR_MODE_NONE,
    0,
    TD_FALSE,
    TD_FALSE,
    {
        TD_FALSE,
        { 0, 0,GC8613_SUPPORT_MAX_W, GC8613_SUPPORT_MAX_H},
    },
};
namespace hisilicon{namespace dev{

    vi_gc8613_4k20_liner::vi_gc8613_4k20_liner(int32_t flag,int32_t w,int32_t h,int32_t fr,int32_t wdr_mode)
        :vi_isp(w,/*w*/
                h,/*h*/
                fr,/*src frame*/
                0,/*vi dev*/
                0,/*mpip dev*/
                0,/*sns clk src*/
                wdr_mode,/*wdr mode*/
                &g_sns_gc8613_4k20_obj,/*sns obj*/
                0/*i2c dev*/,
                flag)
    {
        memcpy(&m_mipi_attr,&g_mipi_4lane_chn0_sensor_gc8613_10bit_8m_nowdr_attr,sizeof(combo_dev_attr_t));
        memcpy(&m_isp_pub_attr,&g_isp_pub_attr_gc8613_mipi_8m_30fps,sizeof(ot_isp_pub_attr));
        m_mipi_attr.img_rect.width = m_w;
        m_mipi_attr.img_rect.height = m_h;
        m_isp_pub_attr.wnd_rect.width = m_w;
        m_isp_pub_attr.wnd_rect.height = m_h;
        m_isp_pub_attr.sns_size.width = m_w;
        m_isp_pub_attr.sns_size.height = m_h;

        m_isp_pub_attr.frame_rate = std::min(20,m_src_fr);
        m_isp_pub_attr.wdr_mode = (m_wdr_mode == 0) ? OT_WDR_MODE_NONE :OT_WDR_MODE_2To1_LINE;

        m_vi_pipe_attr.pixel_format = OT_PIXEL_FORMAT_RGB_BAYER_10BPP;

        //aiisp 需要修改参数
        bool aiisp_enabled = (flag & SYS_FUNCTION_AIISP_ENABLED) ? true : false;
        if(aiisp_enabled)
        {
            m_vi_pipe_attr.compress_mode = OT_COMPRESS_MODE_NONE;
            m_vi_pipe_attr.pixel_format = OT_PIXEL_FORMAT_RGB_BAYER_12BPP;
            m_isp_pub_attr.frame_rate = std::min(12,m_src_fr);
            m_frame_interrupt_attr.interrupt_type = OT_FRAME_INTERRUPT_EARLY_END;
            m_frame_interrupt_attr.early_line = m_vi_pipe_attr.size.height - 100 ;
        }
    }

    vi_gc8613_4k20_liner::~vi_gc8613_4k20_liner()
    {
    }

    vi_gc8613_1080p20_liner::vi_gc8613_1080p20_liner(int32_t flag,int32_t w,int32_t h,int32_t fr,int32_t wdr_mode)
        :vi_isp(w,/*w*/
                h,/*h*/
                fr,/*src frame*/
                0,/*vi dev*/
                0,/*mpip dev*/
                0,/*sns clk src*/
                0,/*wdr mode*/
                &g_sns_gc8613_1080p20_obj,/*sns obj*/
                0/*i2c dev*/,
                flag)
    {
        memcpy(&m_mipi_attr,&g_mipi_4lane_chn0_sensor_gc8613_10bit_8m_nowdr_attr,sizeof(combo_dev_attr_t));
        memcpy(&m_isp_pub_attr,&g_isp_pub_attr_gc8613_mipi_8m_30fps,sizeof(ot_isp_pub_attr));
        m_mipi_attr.img_rect.width = m_w;
        m_mipi_attr.img_rect.height = m_h;
        m_isp_pub_attr.wnd_rect.width = m_w;
        m_isp_pub_attr.wnd_rect.height = m_h;
        m_isp_pub_attr.sns_size.width = m_w;
        m_isp_pub_attr.sns_size.height = m_h;

        m_mipi_attr.mipi_attr.lane_id[0] = 0;
        m_mipi_attr.mipi_attr.lane_id[1] = 1;
        m_mipi_attr.mipi_attr.lane_id[2] = -1;
        m_mipi_attr.mipi_attr.lane_id[3] = -1;

        m_isp_pub_attr.frame_rate = std::min(20,m_src_fr);
        m_isp_pub_attr.wdr_mode = (m_wdr_mode == 0) ? OT_WDR_MODE_NONE :OT_WDR_MODE_2To1_LINE;

        m_vi_pipe_attr.pixel_format = OT_PIXEL_FORMAT_RGB_BAYER_10BPP;

        //aiisp 需要修改参数
        bool aiisp_enabled = (flag & SYS_FUNCTION_AIISP_ENABLED) ? true : false;
        if(aiisp_enabled)
        {
            m_vi_pipe_attr.compress_mode = OT_COMPRESS_MODE_NONE;
            m_vi_pipe_attr.pixel_format = OT_PIXEL_FORMAT_RGB_BAYER_12BPP;
            m_isp_pub_attr.frame_rate = std::min(12,m_src_fr);
            m_frame_interrupt_attr.interrupt_type = OT_FRAME_INTERRUPT_EARLY_END;
            m_frame_interrupt_attr.early_line = m_vi_pipe_attr.size.height - 100 ;
        }
    }

    vi_gc8613_1080p20_liner::~vi_gc8613_1080p20_liner()
    {
    }
        
}}//namespace

