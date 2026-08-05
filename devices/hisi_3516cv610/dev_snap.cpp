#include "dev_sys.h"
#include "dev_snap.h"
#include "dev_log.h"
#include "dev_osd.h"
#include "font_renderer.h"

static const uint32_t JPEG_MAX_VENC_BUF_SIZE =  (3 * 1024 * 1024);

namespace hisilicon{namespace dev{

    snap::snap(std::shared_ptr<vi> vi_ptr)
        :m_vi_ptr(vi_ptr)
    {
        m_bstart = false;

        m_venc_chn_attr.venc_attr.type = OT_PT_JPEG;
        m_venc_chn_attr.venc_attr.max_pic_width = 0;
        m_venc_chn_attr.venc_attr.max_pic_height = 0 ;
        m_venc_chn_attr.venc_attr.pic_width = 0;
        m_venc_chn_attr.venc_attr.pic_height = 0;
        m_venc_chn_attr.venc_attr.buf_size = 0; /* 2 is a number */
        m_venc_chn_attr.venc_attr.is_by_frame = TD_TRUE; /* get stream mode is field mode or frame mode */
        m_venc_chn_attr.venc_attr.profile = 0; 
        m_venc_chn_attr.venc_attr.jpeg_attr.dcf_en = TD_FALSE;
        m_venc_chn_attr.venc_attr.jpeg_attr.mpf_cfg.large_thumbnail_num = 0; 
        m_venc_chn_attr.venc_attr.jpeg_attr.recv_mode = OT_VENC_PIC_RECV_SINGLE;

        m_venc_chn = sys::alloc_venc_chn();
    }

    snap::~snap()
    {
        assert(!m_bstart);
        if(m_venc_chn >= 0)
        {
            sys::free_venc_chn(m_venc_chn);
            m_venc_chn = -1;
        }
    }

    bool snap::start()
    {
        td_s32 ret;

        std::shared_ptr<vi_isp> viisp = std::dynamic_pointer_cast<vi_isp>(m_vi_ptr);
        if(!viisp)
        {
            return false;
        }

        ot_vpss_chn_attr vpss_chn_attr = viisp->vpss_chn_attr();

        m_venc_chn_attr.venc_attr.max_pic_width = vpss_chn_attr.width;
        m_venc_chn_attr.venc_attr.max_pic_height = vpss_chn_attr.height;
        m_venc_chn_attr.venc_attr.pic_width = vpss_chn_attr.width;
        m_venc_chn_attr.venc_attr.pic_height = vpss_chn_attr.height;
        m_venc_chn_attr.venc_attr.buf_size = std::min(m_venc_chn_attr.venc_attr.pic_width * m_venc_chn_attr.venc_attr.pic_height * 3 / 2,JPEG_MAX_VENC_BUF_SIZE);
        ret = ss_mpi_venc_create_chn(m_venc_chn, &m_venc_chn_attr);
        if(ret != TD_SUCCESS) 
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_create_chn failed with %#x", ret);
            return false;
        }

        ot_mpp_chn src_chn;
        ot_mpp_chn dest_chn;
        ot_vpss_grp vpss_grp = m_vi_ptr->vpss_grp();
        ot_vpss_chn vpss_chn = m_vi_ptr->vpss_chn();

        src_chn.mod_id = OT_ID_VPSS;
        src_chn.dev_id = vpss_grp;
        src_chn.chn_id = vpss_chn;

        dest_chn.mod_id = OT_ID_VENC;
        dest_chn.dev_id = 0;
        dest_chn.chn_id = m_venc_chn;

        ret = ss_mpi_sys_bind(&src_chn, &dest_chn);
        if (ret != TD_SUCCESS) 
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_sys_bind faild with %#x",ret);
            ss_mpi_venc_destroy_chn(m_venc_chn);
            return false;
        }
                
        m_bstart = true;
        return true;
    }

    void snap::stop()
    {
        ot_mpp_chn src_chn;
        ot_mpp_chn dest_chn;
        ot_vpss_grp vpss_grp = m_vi_ptr->vpss_grp();
        ot_vpss_chn vpss_chn = m_vi_ptr->vpss_chn();

        src_chn.mod_id = OT_ID_VPSS;
        src_chn.dev_id = vpss_grp;
        src_chn.chn_id = vpss_chn;

        dest_chn.mod_id = OT_ID_VENC;
        dest_chn.dev_id = 0;
        dest_chn.chn_id = m_venc_chn;

        ss_mpi_sys_unbind(&src_chn, &dest_chn);

        //destroy venc
        ss_mpi_venc_stop_chn(m_venc_chn);
        ss_mpi_venc_destroy_chn(m_venc_chn);

        m_bstart = false;
    }

    bool snap::trigger(const char* path,int32_t quality,const char* str_info)
    {
        td_s32 ret;
        td_u32 i;
        td_s32 venc_fd;
        fd_set read_fds;
        struct timeval timeout_val;
        ot_venc_chn_status stat;
        ot_venc_stream stream;

        if(!m_bstart)
        {
            return false;
        }

        ot_venc_start_param start_param;
        start_param.recv_pic_num = 1;
        ret = ss_mpi_venc_start_chn(m_venc_chn, &start_param); 
        if(ret != TD_SUCCESS) 
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_start_chn failed with %#x", ret);
            return false;
        }

        ot_venc_jpeg_param jpg_param;
        ret = ss_mpi_venc_get_jpeg_param(m_venc_chn, &jpg_param);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_get_jpg_param failed with %#x", ret);
            ss_mpi_venc_stop_chn(m_venc_chn);
            return false;
        }

        jpg_param.qfactor = quality;
        ret = ss_mpi_venc_set_jpeg_param(m_venc_chn, &jpg_param);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_set_jpg_param failed with %#x", ret);
            ss_mpi_venc_stop_chn(m_venc_chn);
            return false;
        }

        time_t cur_tm = time(NULL);
        struct tm cur;
        localtime_r(&cur_tm,&cur);
        char data_str[255];
        sprintf(data_str,"%s %04d-%02d-%02d %02d:%02d:%02d",g_week_stsr[cur.tm_wday],cur.tm_year + 1900,cur.tm_mon + 1,cur.tm_mday,cur.tm_hour,cur.tm_min,cur.tm_sec);

        std::shared_ptr<osd_name> osd1 = std::make_shared<osd_name>(10,10,64,m_venc_chn,data_str);
        osd1->start();
        std::shared_ptr<osd_name> osd2;
        if(str_info)
        {
            osd2 = std::make_shared<osd_name>(10,96,64,m_venc_chn,str_info);
            osd2->start();
        }

        venc_fd = ss_mpi_venc_get_fd(m_venc_chn);
        if(venc_fd < 0)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_get_fd failed with %#x", ret);
            ss_mpi_venc_stop_chn(m_venc_chn);
            return false;
        }

        FD_ZERO(&read_fds);
        FD_SET(venc_fd, &read_fds);
        timeout_val.tv_sec = 1;
        timeout_val.tv_usec = 0;
        ret = select(venc_fd + 1, &read_fds, NULL, NULL, &timeout_val);
        if(ret < 0)
        {
            DEV_WRITE_LOG_ERROR("select failed with %#x", ret);
            ss_mpi_venc_stop_chn(m_venc_chn);
            return false;
        }else if(ret == 0)
        {
            DEV_WRITE_LOG_ERROR("select timeout");
            return false;
        }

        ret = ss_mpi_venc_query_status(m_venc_chn, &stat);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_query_status failed with %#x", ret);
            ss_mpi_venc_stop_chn(m_venc_chn);
            return false;
        }

        if(stat.cur_packs == 0)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_query_status cur_packs==0");
            ss_mpi_venc_stop_chn(m_venc_chn);
            return false;
        }

        stream.pack = (ot_venc_pack *)malloc(sizeof(ot_venc_pack) * stat.cur_packs);
        if (stream.pack == NULL)
        {
            DEV_WRITE_LOG_ERROR("malloc failed");
            ss_mpi_venc_stop_chn(m_venc_chn);
            return false;
        }
        stream.pack_cnt = stat.cur_packs;

        ret = ss_mpi_venc_get_stream(m_venc_chn, &stream, -1);
        if(ret != TD_SUCCESS)
        {
            free(stream.pack);
            DEV_WRITE_LOG_ERROR("ss_mpi_get_stream failed with %#x", ret);
            ss_mpi_venc_stop_chn(m_venc_chn);
            return false;
        }

        FILE* f = fopen(path,"wb");
        if(!f)
        {
            free(stream.pack);
            ss_mpi_venc_release_stream(m_venc_chn, &stream);
            ss_mpi_venc_stop_chn(m_venc_chn);
            return false;
        }
        for (i = 0; i < stream.pack_cnt; i++) 
        {
            fwrite(stream.pack[i].addr + stream.pack[i].offset,stream.pack[i].len - stream.pack[i].offset, 1,f);
        }

        fclose(f);
        ss_mpi_venc_release_stream(m_venc_chn, &stream);
        free(stream.pack);
        ss_mpi_venc_stop_chn(m_venc_chn);
        return true;
    }

    bool snap::is_start()
    {
        return m_bstart;
    }

}}//namespace
