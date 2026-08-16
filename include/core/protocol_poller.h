#pragma once

struct ztk_poller_pool;

namespace zero_ipc::core {

/**
 * Shared async event loop for protocol plugins (WebSocket, timers, cross-thread tasks).
 * One ProtocolPoller per process; plugins reuse its worker pool via ProtocolContext.
 */
class ProtocolPoller final {
public:
    ProtocolPoller() = default;
    ~ProtocolPoller();

    ProtocolPoller(const ProtocolPoller&) = delete;
    ProtocolPoller& operator=(const ProtocolPoller&) = delete;

    bool start(int poller_count = 1);
    void stop();

    ztk_poller_pool* poller_pool() const noexcept { return poller_pool_; }

private:
    ztk_poller_pool* poller_pool_{nullptr};
};

} // namespace zero_ipc::core
