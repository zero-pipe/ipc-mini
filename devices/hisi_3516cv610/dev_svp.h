#ifndef dev_svp_include_h
#define dev_svp_include_h

#include <ot_type.h>
#include <svp_acl.h>
#include <svp_acl_mdl.h>
#include <svp_acl_rt.h>
#include <svp_acl_ext.h>

#include "dev_std.h"

namespace hisilicon{namespace dev{

    class svp 
    {
        public:
            static bool init(const char* cfg_file);
            static void release();
    };

}}//namespace

#endif
