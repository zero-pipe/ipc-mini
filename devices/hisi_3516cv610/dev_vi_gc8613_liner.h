#ifndef dev_vi_gc8613_liner_include_h
#define dev_vi_gc8613_liner_include_h

#include "dev_vi_isp.h"

//to support gc8613 liner 
namespace hisilicon{namespace dev{

    class vi_gc8613_4k20_liner
        :public vi_isp
    {
        public:
            vi_gc8613_4k20_liner(int32_t flag,int32_t w,int32_t h,int32_t fr,int32_t wdr_mode);

            virtual ~vi_gc8613_4k20_liner();
    };

    class vi_gc8613_1080p20_liner
        :public vi_isp
    {
        public:
            vi_gc8613_1080p20_liner(int32_t flag,int32_t w,int32_t h,int32_t fr,int32_t wdr_mode);

            virtual ~vi_gc8613_1080p20_liner();
    };

}}//namespace

#endif
