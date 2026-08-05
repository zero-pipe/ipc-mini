#ifndef aiisp_include_h
#define aiisp_include_h

#include <ot_buffer.h>
#include <ot_common_aiisp.h>
#include <ss_mpi_sys_mem.h>
#include <ss_mpi_vb.h>
#include <ss_mpi_vi.h>

class aiisp
{
public:
    aiisp(){}
    virtual ~aiisp(){}

    virtual bool start() = 0;
    virtual void stop() = 0;

    static bool read_model(const char* model_file,ot_aiisp_mem_info* meminfo);
    static ot_vb_pool create_pool(td_u64 blk_size,td_u32 blk_cnt,ot_vb_remap_mode mode);
    static void destroy_pool(ot_vb_pool pool_id);
};

#endif
