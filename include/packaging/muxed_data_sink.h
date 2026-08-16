#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace ipc_mini::packaging {

using MuxedDataSink =
    std::function<bool(const uint8_t* data, std::size_t size)>;

} // namespace ipc_mini::packaging
