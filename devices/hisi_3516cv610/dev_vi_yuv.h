#ifndef dev_vi_yuv_include_h
#define dev_vi_yuv_include_h

#include "dev_vi.h"
//to support yuv input 
namespace hisilicon{namespace dev{

    class vi_yuv_
        :public vi
    {
        public:
            vi_yuv(int32_t w,int32_t h,int32_t src_fr,int32_t vi_dev);
            virtual ~vi_yuv();

            bool start() override;
            void stop() override;
    };

}}//namespace

#endif
