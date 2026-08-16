#pragma once

#include "media_format.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ipc_mini::media {

struct NaluView {
    std::size_t offset{0};
    std::size_t size{0};
};

class MediaFrame final {
public:
    static std::shared_ptr<MediaFrame> create(MediaType type, Codec codec,
                                               int stream_id,
                                               std::size_t payload_size);
    ~MediaFrame() = default;

    MediaFrame(const MediaFrame&) = delete;
    MediaFrame& operator=(const MediaFrame&) = delete;

    MediaType type() const noexcept { return type_; }
    Codec codec() const noexcept { return codec_; }
    int stream_id() const noexcept { return stream_id_; }
    void set_kind(FrameKind kind) noexcept { kind_ = kind; }
    bool keyframe() const noexcept { return kind_ == FrameKind::Key; }
    bool codec_config() const noexcept { return codec_config_; }
    void set_codec_config(bool value) noexcept { codec_config_ = value; }
    int64_t pts_ms() const noexcept { return pts_ms_; }
    int64_t dts_ms() const noexcept { return dts_ms_; }
    void set_timestamps(int64_t pts_ms, int64_t dts_ms) noexcept;

    uint8_t* data() noexcept { return payload_.data(); }
    const uint8_t* data() const noexcept { return payload_.data(); }
    std::size_t size() const noexcept { return payload_.size(); }

    void reserve_nalus(std::size_t count) { nalus_.reserve(count); }
    void add_nalu(std::size_t offset, std::size_t size);
    const std::vector<NaluView>& nalus() const noexcept { return nalus_; }

private:
    MediaFrame(MediaType type, Codec codec, int stream_id,
               std::size_t payload_size);

    MediaType type_;
    Codec codec_;
    int stream_id_;
    FrameKind kind_{FrameKind::Unknown};
    bool codec_config_{false};
    int64_t pts_ms_{0};
    int64_t dts_ms_{0};
    std::vector<uint8_t> payload_;
    std::vector<NaluView> nalus_;
};

} // namespace ipc_mini::media
