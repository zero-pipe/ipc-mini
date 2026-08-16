#ifndef dev_venc_include_h
#define dev_venc_include_h

#include "dev_std.h"
#include <atomic>
#include <stream_observer.h>
#include "dev_aenc.h"

namespace hisilicon{namespace dev{


    class venc;
    typedef std::shared_ptr<venc> venc_ptr;

    class venc
        :public ipc_mini::util::stream_obj
        ,public ipc_mini::util::stream_post
        ,public std::enable_shared_from_this<venc>
    {
        public:
            venc(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t svc_enable,ot_venc_chn src_venc_chn);
            virtual ~venc();

            bool prepare();
            bool start();
            bool resume();
            void pause();
            void stop();
            bool running() const;
            ot_venc_chn venc_chn();
            int32_t venc_fd();
            int32_t venc_w();
            int32_t venc_h();
            int32_t venc_fr();
            int32_t svc_enable();
            virtual void process_video_stream(ot_venc_stream* pstream) = 0;
            void process_audio_stream(ot_audio_stream* pstream);

            bool request_i_frame();

            bool enable_debreath_effect(bool enable,int32_t strength0,int32_t strength1);
            bool enable_intra_refresh(bool enable,int32_t mode,int32_t refresh_num,int32_t request_i_qp);
            
            static bool start_capture();
            static void stop_capture();
            static bool capture_running();

            static bool init();
            static void release();

            static bool get_audio_info(uint8_t* pacode,uint32_t* psample_rate,uint8_t* pbit_width,uint8_t* pchn_cnt);
            static bool enable_audio(bool enable,bool is_mic,const char* name);

        protected:
            static void on_capturing();

        private:
            bool bind_source();
            void unbind_source();

        protected:
            int32_t m_venc_w;
            int32_t m_venc_h;
            int32_t m_src_fr;
            int32_t m_venc_fr;
            ot_venc_chn_attr m_venc_chn_attr;
            ot_venc_chn m_venc_chn;
            ot_vpss_grp m_vpss_grp;
            ot_vpss_chn m_vpss_chn;
            int32_t m_venc_fd;
            int32_t m_svc_enable;
            ot_venc_chn m_src_venc_chn; 
            bool m_created{false};
            bool m_running{false};
            bool m_bound{false};
            
            static std::atomic_bool g_is_capturing;
            static std::thread g_capture_thread;
            static std::list<venc_ptr> g_vencs;
            static aenc_ptr g_aenc_ptr;
    };

    class venc_h264
        :public venc
    {
        public:
            venc_h264(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t svc_enable,ot_venc_chn src_venc_chn);
            virtual ~venc_h264();

            virtual void process_video_stream(ot_venc_stream* pstream);
    };

    class venc_h264_cbr
        :public venc_h264
    {
        public:
            venc_h264_cbr(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t bitrate,int32_t svc_enable,ot_venc_chn src_venc_chn);
            virtual ~venc_h264_cbr();

        protected:
            int32_t m_bitrate;
    };
    
    class venc_h264_avbr
        :public venc_h264
    {
        public:
            venc_h264_avbr(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t max_bitrate,int32_t svc_enable,ot_venc_chn src_venc_chn);
            virtual ~venc_h264_avbr();

        protected:
            int32_t m_max_bitrate;
    };

    class venc_h265
        :public venc
    {
        public:
            venc_h265(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t svc_enable,ot_venc_chn src_venc_chn);
            virtual ~venc_h265();

            virtual void process_video_stream(ot_venc_stream* pstream);
    };

    class venc_h265_cbr
        :public venc_h265
    {
        public:
            venc_h265_cbr(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t bitrate,int32_t svc_enable,ot_venc_chn src_venc_chn);
            virtual ~venc_h265_cbr();

        protected:
            int32_t m_bitrate;
    };

    class venc_h265_avbr
        :public venc_h265
    {
        public:
            venc_h265_avbr(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t max_bitrate,int32_t svc_enable,ot_venc_chn src_venc_chn);
            virtual ~venc_h265_avbr();

        protected:
            int32_t m_max_bitrate;
    };

    class venc_jpg
        :public venc
    {
        public:
            venc_jpg(int32_t chn,int32_t stream,int32_t w,int32_t h,int32_t src_fr,int32_t venc_fr,ot_vpss_grp vpss_grp,ot_vpss_chn vpss_chn,int32_t quality);
            virtual ~venc_jpg();

            virtual void process_video_stream(ot_venc_stream* pstream);
    };

}}//namespace

#endif


