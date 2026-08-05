#ifndef dev_vi_gc4023_liner_include_h
#define dev_vi_gc4023_liner_include_h

#include "dev_vi_isp.h"

//to support gc4023 liner 
namespace hisilicon{namespace dev{

    class vi_gc4023_liner
        :public vi_isp
    {
        public:
            vi_gc4023_liner(int32_t flag,int32_t w,int32_t h,int32_t fr,int32_t wdr_mode);

            virtual ~vi_gc4023_liner();
    };

}}//namespace

#endif
