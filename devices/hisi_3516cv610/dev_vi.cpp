#include "dev_vi.h"
#include "dev_log.h"

namespace hisilicon{namespace dev{

    vi::vi(int32_t w,int32_t h,int32_t src_fr,int32_t vi_dev)
        :m_w(w),m_h(h),m_src_fr(src_fr),m_is_start(false),m_vi_dev(vi_dev),m_vpss_grp(0),m_vpss_chn(0),m_vi_chn(0)
    {
        m_pipes.clear();
        m_pipes.push_back(0);
    }

    vi::~vi()
    {
    }

    int32_t vi::w()
    {
        return m_w;
    }

    int32_t vi::h()
    {
        return m_h;
    }

    int32_t vi::fr()
    {
        return m_src_fr;
    }

    int32_t vi::vi_dev()
    {
        return m_vi_dev;
    }

    int32_t vi::vpss_grp()
    {
        return m_vpss_grp;
    }

    int32_t vi::vpss_chn()
    {
        return m_vpss_chn;
    }

    int32_t vi::vi_chn()
    {
        return m_vi_chn;
    }

    bool vi::init()
    {
        return true;
    }

    void vi::release()
    {
    }

    std::vector<ot_vi_pipe> vi::pipes()
    {
        return m_pipes;
    }

}}//namespace

