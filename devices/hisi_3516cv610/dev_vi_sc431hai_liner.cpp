#include "dev_vi_sc431hai_liner.h"
#include "dev_log.h"

extern ot_isp_sns_obj g_sns_sc431hai_obj;
#define SC431HAI_SUPPORT_MAX_W 2560
#define SC431HAI_SUPPORT_MAX_H 1440

static combo_dev_attr_t g_mipi_4lane_chn0_sensor_sc431hai_10bit_4m_nowdr_attr = {
    .devno = 0,
    .input_mode = INPUT_MODE_MIPI,
    .data_rate = MIPI_DATA_RATE_X1,
    .img_rect = {0, 0, SC431HAI_SUPPORT_MAX_W, SC431HAI_SUPPORT_MAX_H},
    .mipi_attr = {
        DATA_TYPE_RAW_10BIT,
        OT_MIPI_WDR_MODE_NONE,
        {0, 1, 2, 3}
    }
};

static ot_isp_pub_attr g_isp_pub_attr_sc431hai_mipi_4m_30fps = {
    { 0, 0, SC431HAI_SUPPORT_MAX_W, SC431HAI_SUPPORT_MAX_H},
    { SC431HAI_SUPPORT_MAX_W, SC431HAI_SUPPORT_MAX_H},
    30,
    OT_ISP_BAYER_RGGB,
    OT_WDR_MODE_NONE,
    0,
    TD_FALSE,
    TD_FALSE,
    {
        TD_FALSE,
        { 0, 0, SC431HAI_SUPPORT_MAX_W, SC431HAI_SUPPORT_MAX_H},
    },
};

namespace hisilicon{namespace dev{

    vi_sc431hai_liner::vi_sc431hai_liner(int32_t flag,int32_t w,int32_t h,int32_t fr,int32_t wdr_mode)
        :vi_isp(w,/*w*/
                h,/*h*/
                fr,/*src frame*/
                0,/*vi dev*/
                0,/*mpip dev*/
                0,/*sns clk src*/
                0,/*wdr mode,sensor driver not support,force to 0*/
                &g_sns_sc431hai_obj,/*sns obj*/
                0/*i2c dev*/,
                flag)
    {
        memcpy(&m_mipi_attr,&g_mipi_4lane_chn0_sensor_sc431hai_10bit_4m_nowdr_attr,sizeof(combo_dev_attr_t));
        memcpy(&m_isp_pub_attr,&g_isp_pub_attr_sc431hai_mipi_4m_30fps,sizeof(ot_isp_pub_attr));
        m_mipi_attr.img_rect.width = m_w;
        m_mipi_attr.img_rect.height = m_h;
        m_isp_pub_attr.wnd_rect.width = m_w;
        m_isp_pub_attr.wnd_rect.height = m_h;
        m_isp_pub_attr.sns_size.width = m_w;
        m_isp_pub_attr.sns_size.height = m_h;

        m_isp_pub_attr.frame_rate = std::min(30,m_src_fr);
        m_vi_pipe_attr.pixel_format = OT_PIXEL_FORMAT_RGB_BAYER_10BPP;

        //aiisp 需要修改参数
        bool aiisp_enabled = (flag & SYS_FUNCTION_AIISP_ENABLED) ? true : false;
        if(aiisp_enabled)
        {
            m_vi_pipe_attr.compress_mode = OT_COMPRESS_MODE_NONE;
            m_vi_pipe_attr.pixel_format = OT_PIXEL_FORMAT_RGB_BAYER_12BPP;
            m_isp_pub_attr.frame_rate = std::min(12,m_src_fr);
        }
    }

    vi_sc431hai_liner::~vi_sc431hai_liner()
    {
    }

}}//namespace

