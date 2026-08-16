#pragma once

#include <string>

namespace ipc_mini::record {

class ISegmentUploader {
public:
    virtual ~ISegmentUploader() = default;
    virtual void enqueue(const std::string& local_path,
                         const std::string& object_key) = 0;
    virtual void stop() = 0;
};

} // namespace ipc_mini::record
