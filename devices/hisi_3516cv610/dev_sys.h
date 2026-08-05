#ifndef dev_sys_include_h
#define dev_sys_include_h

#include "dev_std.h"

namespace hisilicon{namespace dev{

    enum
    {
        SYS_FUNCTION_AIISP_ENABLED = 1 << 0,
        SYS_FUNCTION_LDC_ENABLED = 1 << 1,
        SYS_FUNCTION_DEBREATH_EFFECT_ENABLED = 1 << 2,
        SYS_FUNCTION_WDR_ENABLE = 1 << 3,
    };

    class sys
    {
        public:
            static bool init(int32_t flag,int32_t max_w,int32_t max_h);
            static void release();
            static void release_unexcepted();

            static ot_venc_chn alloc_venc_chn();
            static void free_venc_chn(ot_venc_chn chn);
            static ot_rgn_handle alloc_rgn_handle();
            static void free_rgn_handle(ot_rgn_handle hdl);
    };

}}//namespace

#endif
