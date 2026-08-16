#pragma once

#include "i_segment_uploader.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace ipc_mini::record {

/** Background HTTP PUT of closed segments / playlist / init. */
class HttpSegmentUploader final : public ISegmentUploader {
public:
    HttpSegmentUploader(std::string base_url, std::string token);
    ~HttpSegmentUploader() override;

    HttpSegmentUploader(const HttpSegmentUploader&) = delete;
    HttpSegmentUploader& operator=(const HttpSegmentUploader&) = delete;

    void enqueue(const std::string& local_path,
                 const std::string& object_key) override;
    void stop() override;

private:
    struct Item {
        std::string local_path;
        std::string object_key;
    };

    void worker_loop();
    bool put_file(const Item& item) const;

    std::string base_url_;
    std::string token_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Item> queue_;
    std::atomic<bool> stopping_{false};
};

class NullSegmentUploader final : public ISegmentUploader {
public:
    void enqueue(const std::string&, const std::string&) override {}
    void stop() override {}
};

} // namespace ipc_mini::record
