#include "dev_sys.h"
#include "dev_osd.h"
#include "dev_log.h"
#include "font_renderer.h"

namespace hisilicon{namespace dev{

        osd::osd(int32_t x,int32_t y,int32_t font_size,ot_venc_chn venc_h)
        :m_x(x),m_y(y),m_font_size(font_size),m_venc_h(venc_h),m_is_start(false)
    {
        m_font_bg_color = rgb24to1555(0,0,0,0);
        m_font_fg_color = rgb24to1555(255,255,255,1);
        m_font_outline_color = rgb24to1555(0,0,0,1);

        memset(&m_rgn_attr,0,sizeof(m_rgn_attr));
        m_rgn_attr.type = OT_RGN_OVERLAY; 
        m_rgn_attr.attr.overlay.pixel_format = OT_PIXEL_FORMAT_ARGB_1555;
        m_rgn_attr.attr.overlay.bg_color = m_font_bg_color;
        m_rgn_attr.attr.overlay.canvas_num = 2;

        memset(&m_rgn_chn_attr,0,sizeof(m_rgn_chn_attr));
        m_rgn_chn_attr.is_show = TD_TRUE;
        m_rgn_chn_attr.type = OT_RGN_OVERLAY;
        m_rgn_chn_attr.attr.overlay_chn.point.x = m_x;
        m_rgn_chn_attr.attr.overlay_chn.point.y = m_y;
        m_rgn_chn_attr.attr.overlay_chn.bg_alpha = 0;
        m_rgn_chn_attr.attr.overlay_chn.fg_alpha = 255;
        m_rgn_chn_attr.attr.overlay_chn.layer = 0; 
        m_rgn_chn_attr.attr.overlay_chn.qp_info.is_abs_qp = TD_FALSE;
        m_rgn_chn_attr.attr.overlay_chn.qp_info.qp_val = 0;
        m_rgn_chn_attr.attr.overlay_chn.qp_info.enable = TD_FALSE;
        m_rgn_chn_attr.attr.overlay_chn.dst = OT_RGN_ATTACH_JPEG_MAIN;

        m_rgn_h = sys::alloc_rgn_handle();
    }

    osd::~osd()
    {
        stop();
    }

    bool osd::init()
    {
        if(!g_freetype.init())
        {
            DEV_WRITE_LOG_ERROR("freetype init failed");
            return false;
        }

        return true;
    }

    void osd::release()
    {
        g_freetype.release();
    }

    bool osd::start()
    {
        if(m_is_start ||
           m_rgn_h == static_cast<ot_rgn_handle>(OT_INVALID_HANDLE))
        {
            return false;
        }

        td_s32 ret = ss_mpi_rgn_create(m_rgn_h,&m_rgn_attr);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_rgn_create failed with 0x%x",ret);
            return false;
        }

        ot_mpp_chn src_chn;
        src_chn.mod_id = OT_ID_VENC;
        src_chn.dev_id = 0;
        src_chn.chn_id = m_venc_h;

        ret = ss_mpi_rgn_attach_to_chn(m_rgn_h, &src_chn, &m_rgn_chn_attr);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_rgn_attach_to_chn failed with 0x%x",ret);
            ss_mpi_rgn_destroy(m_rgn_h);
            return false;
        }

        m_is_start = true;
        return true;
    }

    void osd::stop()
    {
        if(!m_is_start)
        {
            if(m_rgn_h != static_cast<ot_rgn_handle>(OT_INVALID_HANDLE))
            {
                sys::free_rgn_handle(m_rgn_h);
                m_rgn_h = OT_INVALID_HANDLE;
            }
            return ;
        }

        ot_mpp_chn src_chn;
        src_chn.mod_id = OT_ID_VENC;
        src_chn.dev_id = 0;
        src_chn.chn_id = m_venc_h;

        ss_mpi_rgn_detach_from_chn(m_rgn_h, &src_chn);
        ss_mpi_rgn_destroy(m_rgn_h); 

        sys::free_rgn_handle(m_rgn_h);
        m_rgn_h = OT_INVALID_HANDLE;
        m_is_start = false;
    }

    osd_date::osd_date(int32_t x,int32_t y,int32_t font_size,ot_venc_chn venc_h)
        :osd(x,y,font_size,venc_h)
    {
        m_last_date_str[0] = '\0';
    }

    osd_date::~osd_date()
    {
    }

    bool osd_date::start()
    {
        time_t cur_tm = time(NULL);
        struct tm cur;
        localtime_r(&cur_tm,&cur);

        char data_str[32];
        strftime(data_str,sizeof(data_str),"%Y-%m-%d %H:%M:%S",&cur);

        int32_t area_w;
        int32_t area_h;
        g_freetype.get_width(data_str,m_font_size,&area_w);
        area_w = ROUND_UP(area_w,64);
        area_h = ROUND_UP(m_font_size + 4,2);
        //printf("area_w=%d,area_h=%d\n",area_w,area_h);

        m_rgn_attr.attr.overlay.size.width = area_w;
        m_rgn_attr.attr.overlay.size.height = area_h;

        if(!osd::start())
        {
            return false;
        }

        m_last_date_str[0] = '\0';
        m_thd = std::thread(std::bind(&osd_date::on_refresh,this));
        return true;
    }

    void osd_date::on_refresh()
    {
        time_t last_tm = 0;
        time_t cur_tm;
        struct tm cur;
        char cur_osd_date_str[32] = {0};
        td_s32 ret; 
        ot_rgn_canvas_info canvas_info;

        while(m_is_start)
        {
            cur_tm = time(NULL); 
            if(cur_tm == last_tm)
            {
                usleep(100000);
                continue;
            }

            last_tm = cur_tm;

            localtime_r(&cur_tm,&cur);
            strftime(cur_osd_date_str,sizeof(cur_osd_date_str),
                     "%Y-%m-%d %H:%M:%S",&cur);

            ret = ss_mpi_rgn_get_canvas_info(m_rgn_h, &canvas_info);
            if (ret != TD_SUCCESS)
            {
                DEV_WRITE_LOG_ERROR("ss_mpi_rgn_get_canvas_info failed with error 0x%x", ret);
                return ;
            }

            if(strlen(m_last_date_str) == 0)
            {
                //printf("chn=%d,stream=%d,area_w=%d,area_h=%d,%d,%d\n",chn,stream,g_osd_date_info[chn][stream].area_w,g_osd_date_info[chn][stream].area_h,stCanvasInfo.stSize.u32Width,stCanvasInfo.stSize.u32Height);
                g_freetype.show_string(
                        cur_osd_date_str,
                        m_rgn_attr.attr.overlay.size.width,
                        m_rgn_attr.attr.overlay.size.height,
                        m_font_size,
                        (unsigned char*)canvas_info.virt_addr,
                        canvas_info.size.width * canvas_info.size.height * 2,
                        m_font_bg_color,
                        m_font_fg_color,
                        m_font_outline_color);
            }
            else
            {
                g_freetype.show_string_compare(
                        m_last_date_str,
                        cur_osd_date_str,
                        m_rgn_attr.attr.overlay.size.width,
                        m_rgn_attr.attr.overlay.size.height,
                        m_font_size,
                        (unsigned char*)canvas_info.virt_addr, 
                        canvas_info.size.width * canvas_info.size.height * 2,
                        m_font_bg_color,
                        m_font_fg_color,
                        m_font_outline_color);
            }

            std::snprintf(m_last_date_str, sizeof(m_last_date_str), "%s",
                          cur_osd_date_str);

            ret = ss_mpi_rgn_update_canvas(m_rgn_h);
            if (ret != TD_SUCCESS)
            {
                DEV_WRITE_LOG_ERROR("ss_mpi_rgn_update_canvas failed with error 0x%x", ret);
                break;
            }
        }

        DEV_WRITE_LOG_INFO("osd date thread exit");
    }

    void osd_date::stop()
    {
        if(!m_is_start)
        {
            return;
        }
        m_is_start = false;
        if(m_thd.joinable())
        {
            m_thd.join();
        }

        ot_mpp_chn src_chn;
        src_chn.mod_id = OT_ID_VENC;
        src_chn.dev_id = 0;
        src_chn.chn_id = m_venc_h;

        ss_mpi_rgn_detach_from_chn(m_rgn_h, &src_chn);
        ss_mpi_rgn_destroy(m_rgn_h);
        sys::free_rgn_handle(m_rgn_h);
        m_rgn_h = OT_INVALID_HANDLE;
    }

    osd_name::osd_name(int32_t x,int32_t y,int32_t font_size,ot_venc_chn venc_h,const char* name)
        :osd(x,y,font_size,venc_h),m_name(name)
    {
    }

    osd_name::~osd_name()
    {
        stop();
    }

    bool osd_name::start()
    {
        td_s32 ret;
        int32_t area_w;
        int32_t area_h;
        g_freetype.get_width(m_name.c_str(),m_font_size,&area_w);
        area_w = ROUND_UP(area_w,64);
        area_h = ROUND_UP(m_font_size + 4,2);
        //printf("area_w=%d,area_h=%d\n",area_w,area_h);

        m_rgn_attr.attr.overlay.size.width = area_w;
        m_rgn_attr.attr.overlay.size.height = area_h;

        if(!osd::start())
        {
            return false;
        }

        ot_rgn_canvas_info canvas_info;
        ret = ss_mpi_rgn_get_canvas_info(m_rgn_h, &canvas_info);
        if (ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_rgn_get_canvas_info failed with error 0x%x", ret);
            osd::stop();
            return false;
        }

        g_freetype.show_string(
                m_name.c_str(),
                m_rgn_attr.attr.overlay.size.width,
                m_rgn_attr.attr.overlay.size.height,
                m_font_size,
                (unsigned char*)canvas_info.virt_addr,
                canvas_info.size.width * canvas_info.size.height * 2,
                m_font_bg_color,
                m_font_fg_color,
                m_font_outline_color);

        ret = ss_mpi_rgn_update_canvas(m_rgn_h);
        if (ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_rgn_update_canvas failed with error 0x%x", ret);
            osd::stop();
            return false;
        }

        return true;
    }

    void osd_name::stop()
    {
        osd::stop();
    }

}}//namespace


