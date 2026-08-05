#ifndef dev_vi_sc431hai_liner_include_h
#define dev_vi_sc431hai_liner_include_h

#include "dev_vi_isp.h"

//to support sc431hai liner 
namespace hisilicon{namespace dev{

    class vi_sc431hai_liner
        :public vi_isp
    {
        public:
            vi_sc431hai_liner(int32_t flag,int32_t w,int32_t h,int32_t fr,int32_t wdr_mode);

            virtual ~vi_sc431hai_liner();
    };

}}//namespace

#endif
