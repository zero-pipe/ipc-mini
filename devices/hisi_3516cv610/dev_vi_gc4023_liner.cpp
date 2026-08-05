#include "dev_vi_gc4023_liner.h"
#include "dev_log.h"

extern ot_isp_sns_obj g_sns_gc4023_obj;
#define GC4023_SUPPORT_MAX_W 2560
#define GC4023_SUPPORT_MAX_H 1440

static combo_dev_attr_t g_mipi_2lane_chn0_sensor_gc4023_10bit_4m_nowdr_dev1_attr = {
    .devno = 1,
    .input_mode = INPUT_MODE_MIPI,
    .data_rate = MIPI_DATA_RATE_X1,
    .img_rect = {0, 0, GC4023_SUPPORT_MAX_W, GC4023_SUPPORT_MAX_H},
    .mipi_attr = {
        DATA_TYPE_RAW_12BIT,
        OT_MIPI_WDR_MODE_NONE,
        {1, 3, -1, -1}
    }
};

static ot_isp_pub_attr g_isp_pub_attr_gc4023_mipi_4m_30fps = {
    { 0, 0, GC4023_SUPPORT_MAX_W, GC4023_SUPPORT_MAX_H},
    { GC4023_SUPPORT_MAX_W, GC4023_SUPPORT_MAX_H},
    30,
    OT_ISP_BAYER_RGGB,
    OT_WDR_MODE_NONE,
    0,
    TD_FALSE,
    TD_FALSE,
    {
        TD_FALSE,
        { 0, 0, GC4023_SUPPORT_MAX_W, GC4023_SUPPORT_MAX_H},
    },
};

namespace hisilicon{namespace dev{

    vi_gc4023_liner::vi_gc4023_liner(int32_t flag,int32_t w,int32_t h,int32_t fr,int32_t wdr_mode)
        :vi_isp(w,/*w*/
                h,/*h*/
                30,/*src frame*/
                1,/*vi dev*/
                1,/*mpip dev*/
                0,/*sns clk src*/
                0,/*wdr mode,driver not support now,force to 0*/
                &g_sns_gc4023_obj,/*sns obj*/
                0/*i2c dev*/,
                flag)
    {
        memcpy(&m_mipi_attr,&g_mipi_2lane_chn0_sensor_gc4023_10bit_4m_nowdr_dev1_attr,sizeof(combo_dev_attr_t));
        memcpy(&m_isp_pub_attr,&g_isp_pub_attr_gc4023_mipi_4m_30fps,sizeof(ot_isp_pub_attr));
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

    vi_gc4023_liner::~vi_gc4023_liner()
    {
    }

}}//namespace

