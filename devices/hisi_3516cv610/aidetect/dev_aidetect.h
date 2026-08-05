#ifndef dev_aidetect_include_h
#define dev_aidetect_include_h

#include "dev_std.h"
#include "dev_vi.h"
#include "dev_vi_isp.h"
#include "ss_mpi_aidetect.h"
#include <stream_observer.h>


namespace hisilicon{namespace dev{

#define AIDETECT_RECT_NUM 64
    class aidetect 
        :public zero_ipc::util::stream_obj
        ,public zero_ipc::util::stream_post
        ,public std::enable_shared_from_this<aidetect>
    {
    public:
        aidetect(int32_t chn,int32_t stream,std::shared_ptr<vi> vi_ptr,const char* model_path);
        virtual ~aidetect();

        bool start();
        void stop();

        bool is_start();
        int32_t venc_w();
        int32_t venc_h();

    private:
        void on_venc_process();
        void on_process();
        void process_video_stream(ot_venc_stream* pstream);
        void init_result(ot_aidetect_result_array* result);
        void clear_result(ot_aidetect_result_array* result);
        void free_result(ot_aidetect_result_array* result);
        void show_result(ot_aidetect_result_array* result,const ot_video_frame_info *frame_info,td_u32 color);
        bool create_vb_bool();
        void destroy_vb_pool();

    private:
        std::shared_ptr<vi> m_vi_ptr;
        std::string m_model_path;
        bool m_bstart;
        ot_vpss_grp m_vpss_grp;
        ot_vpss_chn  m_vpss_chn;
        ot_vpss_chn_attr m_vpss_chn_attr;
        std::thread m_thread;

        ot_aidetect_chn m_aidetect_chn;
        ot_aidetect_model_info m_aidetect_model;
        ot_aidetect_chn_attr m_aidetect_chn_attr;

        ot_vb_pool m_vb_poolid;

        ot_venc_chn m_venc_chn;
        ot_venc_chn_attr m_venc_chn_attr;
        std::thread m_venc_thread;
    };

}}//namespace

#endif
