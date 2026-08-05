#ifndef dev_osd_include_h
#define dev_osd_include_h

#include "dev_std.h"
#include <atomic>

namespace hisilicon{namespace dev{

    class osd
    {
        public:
            osd(int32_t x,int32_t y,int32_t font_size,ot_venc_chn venc_h);

            virtual ~osd();

            static bool init();
            static void release();

            virtual bool start();
            virtual void stop();

        protected:
            int32_t m_x;
            int32_t m_y;
            int32_t m_font_size;
            ot_rgn_handle m_rgn_h;
            ot_venc_chn m_venc_h;
            ot_rgn_chn_attr m_rgn_chn_attr; 
            ot_rgn_attr m_rgn_attr;
            std::atomic<bool> m_is_start{false};
            int16_t m_font_bg_color;
            int16_t m_font_fg_color;
            int16_t m_font_outline_color;
    };

    class osd_date
        :public osd
    {
        public:
            osd_date(int32_t x,int32_t y,int32_t font_size,ot_venc_chn venc_h);
            virtual ~osd_date();

            virtual bool start() override;
            virtual void stop() override;

        protected:
            void on_refresh();

            std::thread m_thd;
            char m_last_date_str[64];
    };

    class osd_name
        :public osd
    {
        public:
            osd_name(int32_t x,int32_t y,int32_t font_size,ot_venc_chn venc_h,const char* name);
            virtual ~osd_name();

            virtual bool start() override;
            virtual void stop() override;

        protected:
            std::string m_name;
    };

}}//namespace

#endif
