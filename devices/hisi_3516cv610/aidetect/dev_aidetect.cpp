#include "dev_sys.h"
#include "dev_aidetect.h"
#include "dev_log.h"
#include <util/check_interval.h>

static td_char class_types[OT_AIDETECT_CLASS_BUTT][256] = {
    "face",/*人脸*/
    "human",/*人行*/
    "vehicle",/*机动车*/
    "pet",/*宠物*/
    "garbage",/*垃圾袋*/
    "bag",/*快递包装*/
    "wallet",/*钱包*/
    "phone",/*手机*/
    "head_shoulder",/*头肩*/
    "bicycle",/*自行车*/
    "motorcycle",/*电瓶车*/
};

static td_char g_track_status[OT_AIDETECT_TRACK_STATUS_BUTT][256] = {
    "new",/*新目标*/
    "update",/*已跟踪上的目标更新状态*/
    "die",/*已跟踪上的目标丢失*/
    "valid"/*未开启跟踪，返回的状态*/
};

namespace hisilicon{namespace dev{

    aidetect::aidetect(int32_t chn,int32_t stream,std::shared_ptr<vi> vi_ptr,const char* model_path)
        :stream_obj("aidetect_stream",chn,stream),m_vi_ptr(vi_ptr),m_model_path(model_path),m_vb_poolid(OT_VB_INVALID_POOL_ID)
    {
        m_bstart = false;

        memset(&m_vpss_chn_attr,0,sizeof(m_vpss_chn_attr));
        m_vpss_chn_attr.mirror_en                 = TD_FALSE;
        m_vpss_chn_attr.flip_en                   = TD_FALSE;
        m_vpss_chn_attr.border_en                 = TD_FALSE;
        //m_vpss_chn_attr.width                     = 1024;
        //m_vpss_chn_attr.height                    = 576;
        m_vpss_chn_attr.depth                     = 1;
        m_vpss_chn_attr.chn_mode                  = OT_VPSS_CHN_MODE_USER;
        m_vpss_chn_attr.video_format              = OT_VIDEO_FORMAT_LINEAR;
        m_vpss_chn_attr.dynamic_range             = OT_DYNAMIC_RANGE_SDR8;
        m_vpss_chn_attr.pixel_format              = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        m_vpss_chn_attr.compress_mode             = OT_COMPRESS_MODE_NONE;
        m_vpss_chn_attr.aspect_ratio.mode         = OT_ASPECT_RATIO_NONE;

        m_vpss_chn_attr.frame_rate.src_frame_rate = -1;
        m_vpss_chn_attr.frame_rate.dst_frame_rate = -1;

        m_vpss_grp = 0;
        m_vpss_chn = 1;

        m_aidetect_chn = 0;

        m_venc_chn = sys::alloc_venc_chn();

        //init venc chn attr
        memset(&m_venc_chn_attr,0,sizeof(m_venc_chn_attr));
        m_venc_chn_attr.venc_attr.type = OT_PT_H264;
        //m_venc_chn_attr.venc_attr.max_pic_width = m_venc_w;
        //m_venc_chn_attr.venc_attr.max_pic_height = m_venc_h;
        //m_venc_chn_attr.venc_attr.pic_width = m_venc_w;/*the picture width*/
        //m_venc_chn_attr.venc_attr.pic_height    = m_venc_h;/*the picture height*/
        //m_venc_chn_attr.venc_attr.buf_size      = m_venc_w * m_venc_h  *3 / 2;/*stream buffer size*/
        m_venc_chn_attr.venc_attr.is_by_frame      = TD_TRUE;/*get stream mode is slice mode or frame mode?*/
        m_venc_chn_attr.venc_attr.profile = 0;
        m_venc_chn_attr.venc_attr.h264_attr.rcn_ref_share_buf_en = TD_TRUE;
        m_venc_chn_attr.venc_attr.h264_attr.frame_buf_ratio = 70;
        m_venc_chn_attr.gop_attr.gop_mode = OT_VENC_GOP_MODE_NORMAL_P;
        m_venc_chn_attr.gop_attr.normal_p.ip_qp_delta = 2; /* 2 is a number */
        m_venc_chn_attr.rc_attr.rc_mode = OT_VENC_RC_MODE_H264_CBR;
        m_venc_chn_attr.rc_attr.h264_cbr.gop = 25; /*the interval of IFrame*/
        m_venc_chn_attr.rc_attr.h264_cbr.stats_time = 1; /* stream rate statics time(s) */
        m_venc_chn_attr.rc_attr.h264_cbr.src_frame_rate= 1; /* input (vi) frame rate */
        m_venc_chn_attr.rc_attr.h264_cbr.dst_frame_rate = 1; /* target frame rate */
        m_venc_chn_attr.rc_attr.h264_cbr.bit_rate = 1000;
    }

    aidetect::~aidetect()
    {
        assert(!m_bstart);
        if(m_venc_chn >= 0)
        {
            sys::free_venc_chn(m_venc_chn);
            m_venc_chn = -1;
        }
    }

    int32_t aidetect::venc_w()
    {
        return m_venc_chn_attr.venc_attr.pic_width;
    }

    int32_t aidetect::venc_h()
    {
        return m_venc_chn_attr.venc_attr.pic_height;
    }

    bool aidetect::start()
    {
        td_s32 ret;

        std::shared_ptr<vi_isp> viisp = std::dynamic_pointer_cast<vi_isp>(m_vi_ptr);
        if(!viisp)
        {
            return false;
        }

        ot_aidetect_input_model t_input_model_info;
        (td_void)memset_s(&t_input_model_info, sizeof(ot_aidetect_input_model), 0, sizeof(ot_aidetect_input_model));
        t_input_model_info.model_load_mode = OT_AIDETECT_MODEL_LOAD_FROM_PATH;
        t_input_model_info.model = (td_void *)m_model_path.c_str();
        t_input_model_info.size = (td_u32)strlen(m_model_path.c_str());
        (td_void)memset_s(&m_aidetect_chn_attr, sizeof(ot_aidetect_chn_attr), 0, sizeof(ot_aidetect_chn_attr));
        ret = ss_mpi_aidetect_create_chn(m_aidetect_chn, &t_input_model_info, &m_aidetect_chn_attr);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_aidetect_create_chn failed with %#x", ret);
            return false;
        }

        (td_void)memset_s(&m_aidetect_model, sizeof(ot_aidetect_model_info), 0, sizeof(ot_aidetect_model_info));
        ret = ss_mpi_aidetect_get_model_info(m_aidetect_chn, &m_aidetect_model);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_aidetect_get_model_info failed with %#x", ret);
            ss_mpi_aidetect_destroy_chn(m_aidetect_chn);
            return false;
        }

        DEV_WRITE_LOG_INFO("input image:w:%d h:%d classnum:%d",m_aidetect_model.size.width,m_aidetect_model.size.height,m_aidetect_model.class_num);

        m_vpss_chn_attr.width = m_aidetect_model.size.width;
        m_vpss_chn_attr.height = m_aidetect_model.size.height;

         if(!create_vb_bool())
        {
            ss_mpi_aidetect_destroy_chn(m_aidetect_chn);
            return false;
        }

        ret = ss_mpi_vpss_set_chn_vb_src(m_vpss_grp,m_vpss_chn,OT_VB_SRC_USER);
        if (ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vpss_set_chn_vb_src failed with %#x", ret);
            ss_mpi_aidetect_destroy_chn(m_aidetect_chn);
            destroy_vb_pool();
            return false;
        }

        ret = ss_mpi_vpss_attach_chn_vb_pool(m_vpss_grp,m_vpss_chn,m_vb_poolid);
        if (ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vpss_attach_chn_vb_pool failed with %#x", ret);
            ss_mpi_aidetect_destroy_chn(m_aidetect_chn);
            destroy_vb_pool();
            return false;
        }

        ret = ss_mpi_vpss_set_chn_attr(m_vpss_grp, m_vpss_chn, &m_vpss_chn_attr);
        if(ret != TD_SUCCESS) 
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vpss_set_chn_attr failed with %#x", ret);
            ss_mpi_aidetect_destroy_chn(m_aidetect_chn);
            destroy_vb_pool();
            return false;
        }

        ret = ss_mpi_vpss_enable_chn(m_vpss_grp, m_vpss_chn);
        if(ret != TD_SUCCESS) 
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vpss_enable_chn failed with %#x", ret);
            ss_mpi_aidetect_destroy_chn(m_aidetect_chn);
            destroy_vb_pool();
            return false;
        }

        m_venc_chn_attr.venc_attr.max_pic_width = m_vpss_chn_attr.width;
        m_venc_chn_attr.venc_attr.max_pic_height = m_vpss_chn_attr.height;
        m_venc_chn_attr.venc_attr.pic_width = m_vpss_chn_attr.width;
        m_venc_chn_attr.venc_attr.pic_height    = m_vpss_chn_attr.height;
        m_venc_chn_attr.venc_attr.buf_size      = m_vpss_chn_attr.width * m_vpss_chn_attr.height * 3 / 4;
        ret = ss_mpi_venc_create_chn(m_venc_chn,&m_venc_chn_attr);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_venc_create_chn[%d] faild with %#x!",m_venc_chn, ret);
            ss_mpi_aidetect_destroy_chn(m_aidetect_chn);
            destroy_vb_pool();
            return false;
        }

        ot_venc_start_param venc_start_param;
        venc_start_param.recv_pic_num = -1;
        ret = ss_mpi_venc_start_chn(m_venc_chn,&venc_start_param);
        if(ret != TD_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("HI_MPI_VENC_StartRecvPic faild with%#x!", ret);
            ss_mpi_aidetect_destroy_chn(m_aidetect_chn);
            destroy_vb_pool();
            return false;
        }

        m_bstart = true;
        m_venc_thread = std::thread(&aidetect::on_venc_process,this);
        m_thread = std::thread(&aidetect::on_process,this);
        return true;
    }

    void aidetect::init_result(ot_aidetect_result_array* result)
    {
        td_u32 i = 0;

        (td_void)memset_s(result, sizeof(ot_aidetect_result_array), 0, sizeof(ot_aidetect_result_array));
        result->class_num = (m_aidetect_model.class_num > (td_u32)OT_AIDETECT_CLASS_BUTT ? (td_u32)OT_AIDETECT_CLASS_BUTT : m_aidetect_model.class_num);

        for (i = 0; i < result->class_num; ++i)
        {
            result->object_class[i].class_type = m_aidetect_model.classes[i];
            result->object_class[i].object_capacity = 20;
            result->object_class[i].objects = (ot_aidetect_object *)malloc(sizeof(ot_aidetect_object) * result->object_class[i].object_capacity);
            if (result->object_class[i].objects == TD_NULL)
            {
                continue;
            }   

            (td_void)memset_s(result->object_class[i].objects,sizeof(ot_aidetect_object) * result->object_class[i].object_capacity, 0,
                    sizeof(ot_aidetect_object) * result->object_class[i].object_capacity);
        }   
    }

    void aidetect::clear_result(ot_aidetect_result_array* result)
    {
        td_u32 i = 0;
        for (i = 0; i < result->class_num; ++i)
        {
            result->object_class[i].object_num = 0;
            (td_void)memset_s(result->object_class[i].objects,
                    sizeof(ot_aidetect_object) * result->object_class[i].object_capacity, 0,
                    sizeof(ot_aidetect_object) * result->object_class[i].object_capacity);
        }
    }

    void aidetect::free_result(ot_aidetect_result_array* result)
    {
        td_u32 i = 0;
        for (i = 0; i < result->class_num; ++i)
        {
            if (result->object_class[i].objects != TD_NULL) 
            {
                free(result->object_class[i].objects);
                result->object_class[i].objects = TD_NULL;
            }
        }
    }

    void aidetect::show_result(ot_aidetect_result_array* result,const ot_video_frame_info *frame_info,td_u32 color)
    {
        ot_vgs_handle vgs_handle = -1;
        td_s32 ret = TD_FAILURE;
        ot_vgs_task_attr vgs_task;
        ot_cover vgs_add_cover;

        td_u32 i,j;

        bool has_obj = false;
        for(i = 0; i < result->class_num; ++i)
        {
            if(result->object_class[i].object_num > 0)
            {
                has_obj = true;
                break;
            }
        }
        if(!has_obj)
        {
            return;
        }

        ss_mpi_vgs_begin_job(&vgs_handle);

        memcpy_s(&vgs_task.img_out, sizeof(ot_video_frame_info), frame_info, sizeof(ot_video_frame_info));
        memcpy_s(&vgs_task.img_in, sizeof(ot_video_frame_info), frame_info, sizeof(ot_video_frame_info));

        vgs_add_cover.type = OT_COVER_RECT;
        vgs_add_cover.color = color;

        for (i = 0; i < result->class_num; ++i)
        {
            if(result->object_class[i].object_num > 0)
            {
                printf("====class:%s,object_num:%d====\n",class_types[result->object_class[i].class_type],
                        result->object_class[i].object_num);
            }

            for(j = 0; j < result->object_class[i].object_num; j++)
            {
                printf("====\tclass:%s,track id:%u,track_status:%s,rect:[%u,%u,%u,%u]====\n",
                        class_types[result->object_class[i].class_type],
                        result->object_class[i].objects[j].track_id,
                        g_track_status[result->object_class[i].objects[j].track_status],
                        result->object_class[i].objects[j].detect_rect.x,
                        result->object_class[i].objects[j].detect_rect.y,
                        result->object_class[i].objects[j].detect_rect.width,
                        result->object_class[i].objects[j].detect_rect.height);

                vgs_add_cover.rect_attr.is_solid = TD_FALSE; 
                vgs_add_cover.rect_attr.thick = 2; 

                vgs_add_cover.rect_attr.rect.x = ROUND_DOWN(result->object_class[i].objects[j].detect_rect.x,2); 
                vgs_add_cover.rect_attr.rect.y = ROUND_DOWN(result->object_class[i].objects[j].detect_rect.y,2);
                vgs_add_cover.rect_attr.rect.width = ROUND_UP(result->object_class[i].objects[j].detect_rect.width,2); 
                vgs_add_cover.rect_attr.rect.height = ROUND_UP(result->object_class[i].objects[j].detect_rect.height,2); 
                ss_mpi_vgs_add_cover_task(vgs_handle, &vgs_task, &vgs_add_cover, 1);
            }
        }

        ret = ss_mpi_vgs_end_job(vgs_handle);
        if(ret != TD_SUCCESS)
        {
            ss_mpi_vgs_cancel_job(vgs_handle);
        }
    }

    void aidetect::process_video_stream(ot_venc_stream* pstream)
    {
        char* es_buf = NULL;
        int32_t es_len = 0;
        int32_t es_type = 0;
        int32_t nalu_cnt = 0;
        uint64_t time_stamp = 0;

        zero_ipc::util::stream_head sh;

        memset(&sh,0,sizeof(sh));
        sh.type = STREAM_NALU_SLICE;    

        for(td_u32 i = 0; i < pstream->pack_cnt; i++)
        {
            es_buf = (char*)(pstream->pack[i].addr + pstream->pack[i].offset);
            es_len = pstream->pack[i].len - pstream->pack[i].offset;
            es_type = es_buf[4] & 0x1f;
            time_stamp = pstream->pack[i].pts / 1000;

            if(es_type == 0x7 /*sps*/
                    || es_type == 0x8 /*pps*/
                    || es_type == 0x1 /*p*/
                    || es_type == 0x5 /*i*/)
            {
                sh.nalu[nalu_cnt].data = (uint8_t*)es_buf;
                sh.nalu[nalu_cnt].size = es_len; 
                sh.nalu[nalu_cnt].time_stamp = time_stamp;

                nalu_cnt++;
            }
            sh.nalu_count = nalu_cnt;
        }
        post_stream_to_observer(shared_from_this(),&sh,NULL,0);
    }

    void aidetect::on_venc_process()
    {
        fd_set read_fds;
        struct timeval time_val;
        td_s32 ret;
        ot_venc_stream stream;
        ot_venc_chn_status stat;
        td_s32 venc_fd = ss_mpi_venc_get_fd(m_venc_chn);

        while(m_bstart)
        {
            FD_ZERO(&read_fds);
            FD_SET(venc_fd, &read_fds);
                
            time_val.tv_sec  = 0;
            time_val.tv_usec = 10000;
            ret = select(venc_fd + 1, &read_fds, NULL, NULL, &time_val);
            if (ret < 0)
            {
                DEV_WRITE_LOG_ERROR("select faild with %#x!", ret);
                break;
            }
            else if (ret == 0)
            {
                continue;
            }

            memset(&stream, 0, sizeof(stream));
            ret = ss_mpi_venc_query_status(m_venc_chn, &stat);
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
            ret = ss_mpi_venc_get_stream(m_venc_chn, &stream, TD_TRUE);
            if (ret != TD_SUCCESS)
            {
                free(stream.pack);
                stream.pack = NULL;
                DEV_WRITE_LOG_ERROR("ss_mpi_venc_get_stream failed with %#x", ret);
                break;
            }

            process_video_stream(&stream);
            ss_mpi_venc_release_stream(m_venc_chn, &stream);
            free(stream.pack);
            stream.pack = NULL;
        }

        DEV_WRITE_LOG_INFO("thread exit!");
    }

    void aidetect::on_process()
    {
        td_s32 ret;
        ot_video_frame_info frame_info;
        ot_aidetect_result_array result;

        init_result(&result);

        zero_ipc::util::check_interval ci;
        while(m_bstart)
        {
            ret = ss_mpi_vpss_get_chn_frame(m_vpss_grp,m_vpss_chn,&frame_info,1000);
            if(ret != TD_SUCCESS)
            {
                DEV_WRITE_LOG_ERROR("ss_mpi_vpss_get_chn_frame failed with %#x", ret);
                break;
            }

            ci.set_beg();

            clear_result(&result);
            ret = ss_mpi_aidetect_process(m_aidetect_chn, &frame_info.video_frame, &result);
            if(ret != TD_SUCCESS)
            {
                DEV_WRITE_LOG_ERROR("ss_mpi_aidetect_process failed with %#x", ret);
                ss_mpi_vpss_release_chn_frame(m_vpss_grp,m_vpss_chn,&frame_info);
                break;
            }

            show_result(&result,&frame_info,0x0000FF00);
            //printf("interval1=%lld\n",ci.interval());

            ss_mpi_venc_send_frame(m_venc_chn,&frame_info,1000);
            ss_mpi_vpss_release_chn_frame(m_vpss_grp,m_vpss_chn,&frame_info);

            usleep(10000);
        }

        free_result(&result);
    }

    void aidetect::stop()
    {
        if(!m_bstart)
        {
            return;
        }

        m_bstart = false;
        m_thread.join();
        m_venc_thread.join();

        ss_mpi_venc_stop_chn(m_venc_chn);
        ss_mpi_venc_destroy_chn(m_venc_chn);
        ss_mpi_vpss_disable_chn(m_vpss_grp, m_vpss_chn);
        ss_mpi_aidetect_destroy_chn(m_aidetect_chn);
        destroy_vb_pool();
    }

    bool aidetect::is_start()
    {
        return m_bstart;
    }

    bool aidetect::create_vb_bool()
    {
        ot_vb_pool_cfg vb_pool_cfg = { 0 };
        ot_pic_buf_attr pic_buf_attr = {0};
        td_u64 blk_size;

        pic_buf_attr.width = m_vpss_chn_attr.width;
        pic_buf_attr.height = m_vpss_chn_attr.height;
        pic_buf_attr.compress_mode = OT_COMPRESS_MODE_NONE;
        pic_buf_attr.align = OT_DEFAULT_ALIGN;
        pic_buf_attr.bit_width = OT_DATA_BIT_WIDTH_8;
        pic_buf_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        pic_buf_attr.video_format = OT_VIDEO_FORMAT_LINEAR;
        blk_size = ot_common_get_pic_buf_size(&pic_buf_attr);
        vb_pool_cfg.blk_size = blk_size;
        vb_pool_cfg.blk_cnt = 4;
        m_vb_poolid = ss_mpi_vb_create_pool(&vb_pool_cfg);
        if(m_vb_poolid == OT_VB_INVALID_POOL_ID)
        {
            DEV_WRITE_LOG_ERROR("ss_mpi_vb_create_pool failed");
            return false;
        }

        return true;
    }

    void aidetect::destroy_vb_pool()
    {
        if(m_vb_poolid != OT_VB_INVALID_POOL_ID)
        {
            ss_mpi_vb_destroy_pool(m_vb_poolid);
            m_vb_poolid = OT_VB_INVALID_POOL_ID;
        }
    }

}}//namespace
