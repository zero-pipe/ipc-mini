#include "dev_svp.h"
#include "dev_log.h"

namespace hisilicon{namespace dev{

    bool svp::init(const char* cfg_file)
    {
        svp_acl_error ret;
        svp_acl_rt_run_mode run_mode;

        ret = svp_acl_init(cfg_file);
        if(ret != SVP_ACL_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("svp_acl_init failed with error 0x%x",ret);
            return false;
        }

        ret = svp_acl_rt_set_device(0);
        if(ret != SVP_ACL_SUCCESS)
        {
            DEV_WRITE_LOG_ERROR("svp_acl_set device failed with error 0x%x",ret);
            svp_acl_finalize();
            return false;
        }

        ret = svp_acl_rt_get_run_mode(&run_mode);
        if(ret != SVP_ACL_SUCCESS || run_mode != SVP_ACL_DEVICE)
        {
            DEV_WRITE_LOG_ERROR("svp_acl_get_run_mode failed with error 0x%x",ret);
            svp_acl_rt_reset_device(0);
            svp_acl_finalize();
            return false;
        }

        return true;
    }

    void svp::release()
    {
        svp_acl_finalize();
    }

}}//namespace
