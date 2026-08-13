#ifndef dev_aplay_include_h
#define dev_aplay_include_h

#include "dev_std.h"
#include <mutex>

namespace hisilicon {
namespace dev {

/**
 * Speaker playback via AO (PCM).
 * µ-law is decoded in software and pushed with ss_mpi_ao_send_frame.
 * Shares inner acodec clock with AI capture; does not soft-reset acodec.
 * AO starts only on non-silence remote frames (talk), and stops after idle.
 */
class aplay {
public:
    static aplay& instance();

    bool start();
    void stop();
    bool started() const;

    /** Raw µ-law payload (no HiSilicon 4-byte header). */
    bool send_g711u(const uint8_t* data, size_t len);

private:
    aplay() = default;
    ~aplay();

    aplay(const aplay&) = delete;
    aplay& operator=(const aplay&) = delete;

    bool configure_acodec_output();
    bool send_pcm_locked(const int16_t* pcm, td_u32 bytes);

    mutable std::mutex mutex_;
    bool started_{false};
    unsigned silence_frames_{0};
    ot_aio_attr aio_attr_{};
    ot_audio_dev ao_dev_{0};
    ot_ao_chn ao_chn_{0};
};

} // namespace dev
} // namespace hisilicon

#endif
