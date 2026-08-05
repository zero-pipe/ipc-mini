#ifndef dev_aplay_include_h
#define dev_aplay_include_h

#include "dev_std.h"
#include <mutex>

namespace hisilicon {
namespace dev {

/**
 * G711U decode + AO speaker playback (ADEC → AO bind).
 * Shares inner acodec clock with AI capture; does not soft-reset acodec.
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

    mutable std::mutex mutex_;
    bool started_{false};
    ot_aio_attr aio_attr_{};
    ot_adec_chn_attr adec_attr_{};
    ot_adec_attr_g711 adec_g711_{};
    ot_audio_dev ao_dev_{0};
    ot_ao_chn ao_chn_{0};
    ot_adec_chn adec_chn_{0};
};

} // namespace dev
} // namespace hisilicon

#endif
