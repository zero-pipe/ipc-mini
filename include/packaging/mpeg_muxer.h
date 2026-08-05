#pragma once

#include "muxed_data_sink.h"
#include "media/media_frame.h"
#include <memory>

namespace zero_ipc::packaging {

enum class MpegContainer {
    TransportStream,
    ProgramStream,
};

class MpegMuxer final {
public:
    MpegMuxer(MuxedDataSink output,
              MpegContainer container = MpegContainer::TransportStream);
    ~MpegMuxer();

    MpegMuxer(const MpegMuxer&) = delete;
    MpegMuxer& operator=(const MpegMuxer&) = delete;

    bool add_track(media::Codec codec);
    bool write_frame(const std::shared_ptr<const media::MediaFrame>& frame);
    void close();

private:
    struct Context;
    static void* allocate_packet(void* param, std::size_t bytes);
    static void free_packet(void* param, void* packet);
    static int write_packet(void* param, int stream, void* packet, std::size_t bytes);

    MuxedDataSink output_;
    std::unique_ptr<Context> context_;
};

} // namespace zero_ipc::packaging
