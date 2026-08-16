#pragma once

#include "encoded_frame.h"
#include "media/media_source.h"

#include <cerrno>
#include <cstdint>
#include <memory>
#include <thread>

#pragma push_macro("errno")
#undef errno
#include <stream_observer.h>
#pragma pop_macro("errno")

namespace zero_ipc::device_adapter {

/** Publishes the AI preview bitstream (stream_id=2) into MediaSource. */
class AiStreamPublisher final : public zero_ipc::util::stream_observer {
public:
    explicit AiStreamPublisher(std::shared_ptr<media::MediaSource> media_source);

    bool register_track(int width, int height, int frame_rate);
    void on_stream_come(zero_ipc::util::stream_obj_ptr stream,
                        zero_ipc::util::stream_head* head,
                        const char* buffer, int32_t size) override;
    void on_stream_error(zero_ipc::util::stream_obj_ptr stream,
                         int32_t error_code) override;

private:
    std::shared_ptr<media::MediaSource> media_source_;
};

} // namespace zero_ipc::device_adapter
