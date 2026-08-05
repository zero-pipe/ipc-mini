#ifndef aiisp_bnr_include_h
#define aiisp_bnr_include_h

#include "aiisp.h"
#include <ot_common_aibnr.h>

#define MAX_MODEL_NUM 2
#define LOW_ISO_MODEL_IDX 0
#define HIGH_ISO_MODEL_IDX 1

class aiisp_bnr
    :public aiisp
{
public:
    aiisp_bnr(int32_t pipe);

    ~aiisp_bnr() override;

    static bool init(const char* low_iso_file,const char* high_iso_file,int32_t w,int32_t h,int32_t is_wdr_mode);

    static void release();

    bool start() override;

    void stop() override;

private:
    int32_t m_pipe;

private:
    static ot_aibnr_model g_model_info[MAX_MODEL_NUM];
    static td_s32 g_model_id[MAX_MODEL_NUM];
    static ot_vb_pool g_aiisp_bnr_pool;
};

#endif
