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

namespace ipc_mini::device_adapter {

/** Publishes the AI preview bitstream (stream_id=2) into MediaSource. */
class AiStreamPublisher final : public ipc_mini::util::stream_observer {
public:
    explicit AiStreamPublisher(std::shared_ptr<media::MediaSource> media_source);

    bool register_track(int width, int height, int frame_rate);
    void on_stream_come(ipc_mini::util::stream_obj_ptr stream,
                        ipc_mini::util::stream_head* head,
                        const char* buffer, int32_t size) override;
    void on_stream_error(ipc_mini::util::stream_obj_ptr stream,
                         int32_t error_code) override;

private:
    std::shared_ptr<media::MediaSource> media_source_;
};

} // namespace ipc_mini::device_adapter
