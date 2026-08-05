#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace zero_ipc::packaging {

using MuxedDataSink =
    std::function<bool(const uint8_t* data, std::size_t size)>;

} // namespace zero_ipc::packaging
