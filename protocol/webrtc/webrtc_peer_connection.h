#pragma once

#include "media/detection.h"
#include "media/media_frame.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ipc_mini::webrtc_net {

struct IceServerConfig {
    std::string urls;  // stun:host:3478  or  turn:host:3478?transport=udp
    std::string username;
    std::string credential;
};

struct PeerConnectionConfig {
    std::vector<IceServerConfig> ice_servers;
    int rolling_buffer_duration_sec{1};
    int expected_bitrate_bps{1000 * 1000};
    bool disable_twcc{true};
    bool enable_datachannel{true};
    std::string datachannel_label{"detect"};
};

/**
 * One WebRTC PeerConnection (SDP / ICE / RTP / DataChannel).
 * Media stack is Amazon KVS WebRTC when built with IPC_MINI_ENABLE_KVS=1.
 * Without KVS, methods return false / no-op so signaling bring-up still builds.
 */
class WebRtcPeerConnection final {
public:
    using LocalCandidateHandler =
        std::function<void(const std::string& candidate_json)>;
    using LocalSdpHandler =
        std::function<void(const std::string& type, const std::string& sdp)>;
    using KeyframeHandler = std::function<void()>;
    using StateHandler = std::function<void(const std::string& state)>;
    using RemoteAudioHandler =
        std::function<void(const uint8_t* data, size_t len)>;

    explicit WebRtcPeerConnection(PeerConnectionConfig config);
    ~WebRtcPeerConnection();

    bool start();
    /** Disable sends/callbacks before handing blocking destruction to reaper. */
    void deactivate();
    void stop();

    bool handle_remote_offer(const std::string& sdp);
    bool handle_remote_answer(const std::string& sdp);
    bool handle_remote_candidate(const std::string& candidate,
                                 const std::string& sdp_mid,
                                 int sdp_mline_index);

    bool write_video_frame(const std::shared_ptr<const ipc_mini::media::MediaFrame>& frame);
    bool write_audio_frame(const std::shared_ptr<const ipc_mini::media::MediaFrame>& frame);
    bool send_detections(const ipc_mini::media::DetectionResult& detections);

    void set_local_candidate_handler(LocalCandidateHandler handler);
    void set_local_sdp_handler(LocalSdpHandler handler);
    void set_keyframe_handler(KeyframeHandler handler);
    void set_state_handler(StateHandler handler);
    void set_remote_audio_handler(RemoteAudioHandler handler);

    bool media_ready() const { return media_ready_.load(); }

private:
    class CallbackGuard {
    public:
        explicit CallbackGuard(WebRtcPeerConnection* owner);
        ~CallbackGuard();
    private:
        WebRtcPeerConnection* owner_;
    };

    void wait_for_callbacks();

    PeerConnectionConfig config_;
    std::mutex mutex_;
    std::mutex callback_mutex_;
    std::condition_variable callback_cv_;
    unsigned callbacks_inflight_{0};
    LocalCandidateHandler on_candidate_;
    LocalSdpHandler on_sdp_;
    KeyframeHandler on_keyframe_;
    StateHandler on_state_;
    RemoteAudioHandler on_remote_audio_;
    std::atomic<bool> media_ready_{false};
    std::atomic<bool> dc_open_{false};
    void* peer_connection_{nullptr}; // PRtcPeerConnection when KVS enabled
    void* video_transceiver_{nullptr};
    void* audio_transceiver_{nullptr};
    void* data_channel_{nullptr};
    /** Monotonic audio PTS in 100ns units (sample-clocked, not encoder PTS). */
    uint64_t audio_pts_100ns_{0};
    bool started_{false};
};

bool kvs_runtime_available();

} // namespace ipc_mini::webrtc_net
