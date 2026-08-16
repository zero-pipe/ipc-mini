#include "core/protocol_poller.h"

#include <ztk/poller/poller_pool.h>

namespace zero_ipc::core {

ProtocolPoller::~ProtocolPoller()
{
    stop();
}

bool ProtocolPoller::start(int poller_count)
{
    if (poller_pool_) {
        return true;
    }
    if (poller_count < 1) {
        poller_count = 1;
    }
    ztk_poller_pool_opts_t opts{};
    opts.size = poller_count;
    /* Keep the embedded runtime on one dedicated poller thread. */
    opts.prefer_current_thread = 0;
    opts.thread_priority = ZTK_THREAD_PRIO_NORMAL;
    auto* poller_pool = ztk_poller_pool_create(&opts);
    if (!poller_pool || ztk_poller_pool_start(poller_pool) != ZTK_OK) {
        if (poller_pool) {
            ztk_poller_pool_destroy(poller_pool);
        }
        return false;
    }
    poller_pool_ = poller_pool;
    return true;
}

void ProtocolPoller::stop()
{
    if (!poller_pool_) {
        return;
    }
    ztk_poller_pool_stop(poller_pool_);
    ztk_poller_pool_destroy(poller_pool_);
    poller_pool_ = nullptr;
}

} // namespace zero_ipc::core
