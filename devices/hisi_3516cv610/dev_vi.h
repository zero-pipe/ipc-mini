#ifndef dev_vi_include_h
#define dev_vi_include_h

#include "dev_std.h"

namespace hisilicon{namespace dev{

    class vi
    {
        public:
            vi(int32_t w,int32_t h,int32_t src_fr,int32_t vi_dev);
            virtual ~vi();

            virtual bool start() = 0;
            virtual void stop() = 0;

            static bool init();
            static void release();

            int32_t w();
            int32_t h();
            int32_t fr();
            int32_t vi_dev();
            std::vector<ot_vi_pipe> pipes();
            int32_t vpss_grp();
            int32_t vpss_chn();
            int32_t vi_chn();

        protected:
            int32_t m_w;
            int32_t m_h;
            int32_t m_src_fr;
            bool m_is_start;
            int32_t m_vi_dev;

            std::vector<ot_vi_pipe> m_pipes;
            ot_vpss_grp m_vpss_grp;
            ot_vpss_chn m_vpss_chn;
            ot_vi_chn m_vi_chn;
    };

}}//namespace

#endif
