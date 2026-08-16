#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace ipc_mini::packaging {

enum class FlvTagType : uint8_t {
    Audio = 8,
    Video = 9,
    Script = 18,
};

using FlvTagSink =
    std::function<bool(FlvTagType type, const uint8_t* data,
                       std::size_t size, uint32_t timestamp)>;

} // namespace ipc_mini::packaging
