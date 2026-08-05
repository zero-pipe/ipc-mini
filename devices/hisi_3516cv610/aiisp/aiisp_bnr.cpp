#include <util/std.h>
#include "aiisp_bnr.h"
#include "dev_std.h"
#include <ss_mpi_aibnr.h>

ot_aibnr_model aiisp_bnr::g_model_info[MAX_MODEL_NUM];
td_s32 aiisp_bnr::g_model_id[MAX_MODEL_NUM] = {-1,-1};
ot_vb_pool aiisp_bnr::g_aiisp_bnr_pool = OT_VB_INVALID_POOL_ID;

aiisp_bnr::aiisp_bnr(int32_t pipe)
    :m_pipe(pipe)
{
}

aiisp_bnr::~aiisp_bnr()
{
}

bool aiisp_bnr::init(const char* low_iso_file,const char* high_iso_file,int32_t w,int32_t h,int32_t is_wdr_mode)
{
    td_s32 ret = 0;

    ret = ss_mpi_aibnr_init();
    if(ret != TD_SUCCESS)
    {
        printf("[%s]: ss_mpi_aibnr_init failed with error 0x%x\n",__FUNCTION__,ret);
        return false;
    }

    memset(&g_model_info,0,sizeof(g_model_info));

    //low iso model
    if(!aiisp::read_model(low_iso_file,&g_model_info[LOW_ISO_MODEL_IDX].model.mem_info))
    {
        printf("[%s]: read_model failed\n",__FUNCTION__);
        return false;
    }

    //high iso model
    if(!aiisp::read_model(high_iso_file,&g_model_info[HIGH_ISO_MODEL_IDX].model.mem_info))
    {
        printf("[%s]: read_model failed\n",__FUNCTION__);
        return false;
    }

    for(int32_t i = 0; i < MAX_MODEL_NUM; i++)
    {
        g_model_info[i].model.preempted_en = TD_FALSE;
        g_model_info[i].model.image_size.width = w;
        g_model_info[i].model.image_size.height = h;
        g_model_info[i].is_wdr_mode = TD_FALSE;//cv610 not support wdr
        g_model_info[i].ref_mode = OT_AIBNR_REF_MODE_NORM; 
        g_model_info[i].nlc_en = TD_FALSE;
        ret = ss_mpi_aibnr_load_model(&g_model_info[i],&g_model_id[i]);
        if(ret != TD_SUCCESS)
        {
            printf("[%s]: ss_mpi_aibnr_load model failed with error 0x%x\n",__FUNCTION__,ret);
            return false;
        }
    }
   
    td_s32 blk_size;
    blk_size = ot_aibnr_get_pic_buf_size(w, h);

    /**
     * 以2560x1440为例子，blksize=5529600字节=5M大小，如果开4个vb快，mmz需要5Mx4=20M
     */
    g_aiisp_bnr_pool = aiisp::create_pool(blk_size,5,OT_VB_REMAP_MODE_NONE);
    if(g_aiisp_bnr_pool == OT_VB_INVALID_POOL_ID)
    {
        printf("[%s]: ss_mpi_vb_create_pool with error 0x%x\n",__FUNCTION__,ret);
        return false;
    }

    return true;
}

void aiisp_bnr::release()
{
    for(int32_t i = 0; i < MAX_MODEL_NUM; i++)
    {
        if(g_model_id[i] != -1)
        {
            ss_mpi_aibnr_unload_model(g_model_id[i]);
        }
    }

    ss_mpi_aibnr_exit();

    for(int32_t i = 0; i < MAX_MODEL_NUM; i++)
    {
        if(g_model_info[i].model.mem_info.phys_addr != 0)
        {
            ss_mpi_sys_mmz_free(g_model_info[i].model.mem_info.phys_addr, g_model_info[i].model.mem_info.virt_addr);
        }
    }

    if (g_aiisp_bnr_pool != OT_VB_INVALID_POOL_ID)
    {
        aiisp::destroy_pool(g_aiisp_bnr_pool);
        g_aiisp_bnr_pool = OT_VB_INVALID_POOL_ID;
    }
}

bool aiisp_bnr::start()
{
    td_s32 ret;
    
    ot_aiisp_pool pool_attr;
    memset(&pool_attr,0,sizeof(pool_attr));
    pool_attr.aiisp_type = OT_AIISP_TYPE_AIBNR;
    pool_attr.aibnr_pool.vb_pool = g_aiisp_bnr_pool;
    ret = ss_mpi_vi_attach_aiisp_vb_pool(m_pipe, &pool_attr);
    if(ret != TD_SUCCESS)
    {
        printf("[%s]: ss_mpi_vi_attach_aiisp_vb_pool failed with error 0x%x\n",__FUNCTION__,ret);
        return false;
    }

    ot_isp_black_level_attr black_level_attr;
    ret = ss_mpi_isp_get_black_level_attr(m_pipe, &black_level_attr);
    if(ret != TD_SUCCESS)
    {
        printf("[%s]: ss_mpi_isp_get_black_level_attr failed with error 0x%x\n",__FUNCTION__,ret);
        return false;
    }
    black_level_attr.user_black_level_en = TD_TRUE;
    for (int32_t i = 0; i < OT_ISP_WDR_MAX_FRAME_NUM; i++)
    {
        for (int32_t j = 0; j < OT_ISP_BAYER_CHN_NUM; j++)
        {
            black_level_attr.user_black_level[i][j] = 1200; /* user_black_level of aibnr default as 1200 */
        }
    }
    ret = ss_mpi_isp_set_black_level_attr(m_pipe, &black_level_attr);
    if (ret != TD_SUCCESS)
    {
        printf("[%s]: ss_mpi_isp_set_black_level_attr failed with error 0x%x\n",__FUNCTION__,ret);
        return false;
    }

    ret = ss_mpi_aibnr_enable(m_pipe);
    if(ret != TD_SUCCESS)
    {
        printf("[%s]: ss_mpi_aibnr_start failed with error 0x%x\n",__FUNCTION__,ret);
        return false;
    }

    ot_aibnr_attr aibnr_attr;
    ret = ss_mpi_aibnr_get_attr(m_pipe, &aibnr_attr);
    if(ret != TD_SUCCESS)
    {
        printf("[%s]: ss_mpi_aibnr_get_attr failed with error 0x%x\n",__FUNCTION__,ret);
        return false;
    }

    aibnr_attr.enable = TD_TRUE;
    aibnr_attr.bnr_bypass = TD_FALSE;
    aibnr_attr.blend = TD_FALSE;
    aibnr_attr.op_type = OT_OP_MODE_MANUAL;
    aibnr_attr.manual_attr.sfs = 31; /* sfs: 31 */
    aibnr_attr.manual_attr.tfs = 31; /* tfs: 31 */
    ret = ss_mpi_aibnr_set_attr(m_pipe, &aibnr_attr);
    if(ret != TD_SUCCESS)
    {
        printf("[%s]: ss_mpi_aibnr_set_attr failed with error 0x%x\n",__FUNCTION__,ret);
        return false;
    }

    ot_aibnr_model_attr model_attr;
    memset(&model_attr,0,sizeof(model_attr));
    model_attr.op_type = OT_OP_MODE_AUTO;
    for (td_u32 i = 0; i < OT_AIISP_AUTO_ISO_NUM; i++)
    {
        /**
         * 数组序号和iso对应关系：
         [100,200,400,800,1600,3200,6400,12800,25600,51200,102400,204800,409600,819200,1638400,3276800]
         */
        if (i < 0x2)
        {
            //iso 100,200使用low iso
            model_attr.auto_id[i] = LOW_ISO_MODEL_IDX;
        }
        else
        {
            //iso 400以上使用high iso
            model_attr.auto_id[i] = HIGH_ISO_MODEL_IDX;
        }
    }
    ret = ss_mpi_aibnr_set_model_attr(TD_FALSE, &model_attr);
    if(ret != TD_SUCCESS)
    {
        printf("[%s]: ss_mpi_aibnr_set_model_attr failed with error 0x%x\n",__FUNCTION__,ret);
        return false;
    }

    return true;
}

void aiisp_bnr::stop()
{
    td_s32 ret;

    ret = ss_mpi_aibnr_disable(m_pipe);
    if(ret != TD_SUCCESS)
    {
        printf("[%s]: ss_mpi_aibnr_stop failed with error 0x%x\n",__FUNCTION__,ret);
        return ;
    }

    return ;
}


