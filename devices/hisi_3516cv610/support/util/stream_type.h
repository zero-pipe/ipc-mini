#ifndef stream_type_include_h
#define stream_type_include_h

#include <stdint.h>

namespace ipc_mini{namespace util{

/* Callback tag values for stream_head::type (keep numeric ABI stable). */
#define STREAM_P_FRAME 0
#define STREAM_I_FRAME 1
#define STREAM_AUDIO_FRAME 2
#define STREAM_B_FRAME 3       /* reserved for B; current VENC GOP does not emit */
#define STREAM_NALU_SLICE 4    /* H.264/H.265 path used by this project today */
#define STREAM_MJPEG_FRAME 6

#define IS_VIDEO_FRAME(n) ((n) == STREAM_I_FRAME  \
|| (n)== STREAM_P_FRAME  \
||(n)== STREAM_B_FRAME  \
||(n)== STREAM_NALU_SLICE \
||(n) == STREAM_MJPEG_FRAME)

#define IS_LOCATE_FRAME(n)((n) == STREAM_I_FRAME \
|| (n) == STREAM_MJPEG_FRAME)

#define IS_AUDIO_FRAME(n)((n) == STREAM_AUDIO_FRAME)

#define MAX_STREAM_NALU_COUNT (8)
    typedef struct
    {
        uint8_t* data;
        uint32_t size;
        uint32_t time_stamp;
    }nalu_t;

    /*
     * stream_head::type must match STREAM_* above:
     *   0 P, 1 I, 2 audio, 3 B, 4 NALU_SLICE, 6 MJPEG
     *
     * This project's H.264/H.265 VENC currently sets type=STREAM_NALU_SLICE
     * (I/P distinguished later via NALU). GOP=NORMAL_P + Baseline => no B
     * frames today; STREAM_B_FRAME remains for a future GOP/profile change.
     */
    typedef struct {
        uint32_t len;
        uint32_t type;
        uint32_t time_stamp;

        uint32_t nalu_count;
        nalu_t nalu[MAX_STREAM_NALU_COUNT];
    }stream_head;

    enum
    {
        STREAM_VIDEO_ENCODE_H264 = 0,
        STREAM_VIDEO_ENCODE_MJPEG = 1,
        STREAM_VIDEO_ENCODE_H265 = 2,
    };

    enum
    {
        STREAM_AUDIO_ENCODE_NONE = 0x0,
        STREAM_AUDIO_ENCODE_G711U = 0x1,
        STREAM_AUDIO_ENCODE_AAC = 0x2,
    };

#define ipc_mini_TAG 0x5550564e
    typedef struct
    {
        uint32_t  media_fourcc;

        struct
        {
            uint8_t vcode;
            uint16_t w;
            uint16_t h;
            uint16_t fr;
        }video_info;

        struct
        {
            uint8_t acode;
            uint32_t sample_rate;
            uint8_t bit_width;
            uint8_t chn;
        }audio_info;

    }media_head;

}}//namespace

#endif

