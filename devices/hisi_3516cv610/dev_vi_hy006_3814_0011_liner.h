#ifndef dev_vi_hy006_3814_0011_liner_include_h
#define dev_vi_hy006_3814_0011_liner_include_h

#include "dev_vi_isp.h"

//to support hy006_3814_0011 liner 
namespace hisilicon{namespace dev{

    class vi_hy006_3814_0011_liner
        :public vi_isp
    {
        public:
            vi_hy006_3814_0011_liner(int32_t flag,int32_t w,int32_t h,int32_t fr,int32_t wdr_mode);

            virtual ~vi_hy006_3814_0011_liner();
    };

}}//namespace

#endif
