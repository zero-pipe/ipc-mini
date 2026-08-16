#ifndef dev_chn_include_h
#define dev_chn_include_h

#include "dev_sys.h"
#include "dev_vi_sc4336p_liner.h"
#include "dev_vi_gc8613_liner.h"
#include "dev_vi_gc4023_liner.h"
#include "dev_vi_sc431hai_liner.h"
#include "dev_vi_hy006_3814_0011_liner.h"
#include "dev_venc.h"
#include "dev_osd.h"
#include "dev_log.h"
#include <stream_observer.h>

#define MAX_CHANNEL 1

namespace hisilicon{namespace dev{

#define MAIN_STREAM_ID 0
#define SUB_STREAM_ID 1
#define AI_STREAM_ID 2

    struct time_osd_options
    {
        bool enable{false};
        int32_t x{32};
        int32_t y{32};
        int32_t font_size{32};
    };

    struct venc_encode_options
    {
        bool enable{true};
        int32_t width{1280};
        int32_t height{720};
        int32_t frame_rate{15};
        int32_t bitrate_kbps{800};
        int32_t svc_enable{0};
        time_osd_options osd;
    };

    class chn 
        :public ipc_mini::util::stream_observer
         ,public std::enable_shared_from_this<chn>
    {
        public:
            chn(const char* vi_name,const char*venc_mode,int32_t chn_no);
            ~chn();

            bool start(int32_t venc_w,int32_t venc_h,int32_t fr,int32_t bitrate,
                       int32_t svc_enable,const time_osd_options& time_osd);
            bool start(const venc_encode_options& main,
                       const venc_encode_options& sub);
            void stop();
            bool is_start();
            bool set_sub_stream_enabled(bool enable);
            std::shared_ptr<vi> video_input() const;
            ot_venc_chn svc_venc_chn() const;

            static bool init(int32_t flag,lane_divide_mode_t lane_mode,int32_t max_w,int32_t max_h,int32_t vi_fr,int32_t wdr_mode);
            static void release();

            static void start_capture(bool enable);

            void on_stream_come(ipc_mini::util::stream_obj_ptr sobj,ipc_mini::util::stream_head* head, const char* buf, int32_t len);
            void on_stream_error(ipc_mini::util::stream_obj_ptr sobj,int32_t errno);

            //for audio
            static bool enable_audio(bool enable,bool is_mic,const char* name);
            static bool enable_audio_play(bool enable);
            static bool play_g711u(const uint8_t* data, size_t len);

            static bool request_i_frame(int32_t chn,int32_t stream);

        private:
            bool m_is_start;
            std::string m_vi_name;
            std::shared_ptr<vi> m_vi_ptr;
            std::shared_ptr<venc> m_venc_main_ptr;
            std::shared_ptr<venc> m_venc_sub_ptr;
            std::shared_ptr<osd_date> m_time_osd_main;
            std::shared_ptr<osd_date> m_time_osd_sub;
            int32_t m_chn;
            std::string m_venc_mode;
            bool m_sub_stream_running{false};

            static std::shared_ptr<chn> g_chns[MAX_CHANNEL];
            static int32_t g_sys_flag;
            static int32_t g_vi_max_w;
            static int32_t g_vi_max_h;
            static int32_t g_vi_fr;
            static int32_t g_vi_wdr_mode;
            static bool g_aenc_enable;
            static std::string g_aenc_name;
    };

}}//namespace

#endif
