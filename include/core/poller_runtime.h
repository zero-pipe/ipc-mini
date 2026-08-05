#pragma once

struct ztk_poller_pool;

namespace zero_ipc::core {

/**
 * Owns the shared poller pool for all protocol sessions (SRP).
 * One PollerRuntime per process; plugins reuse its worker pool.
 */
class PollerRuntime final {
public:
    PollerRuntime() = default;
    ~PollerRuntime();

    PollerRuntime(const PollerRuntime&) = delete;
    PollerRuntime& operator=(const PollerRuntime&) = delete;

    bool start(int poller_count = 1);
    void stop();

    ztk_poller_pool* poller_pool() const noexcept { return poller_pool_; }

private:
    ztk_poller_pool* poller_pool_{nullptr};
};

} // namespace zero_ipc::core
