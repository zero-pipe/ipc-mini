#include "webrtc_peer_connection.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#if defined(IPC_MINI_ENABLE_KVS) && IPC_MINI_ENABLE_KVS
#include <com/amazonaws/kinesis/video/webrtcclient/Include.h>
#endif

namespace ipc_mini::webrtc_net {

bool kvs_runtime_available()
{
#if defined(IPC_MINI_ENABLE_KVS) && IPC_MINI_ENABLE_KVS
    return true;
#else
    return false;
#endif
}

WebRtcPeerConnection::WebRtcPeerConnection(PeerConnectionConfig config)
    : config_(std::move(config))
{
}

WebRtcPeerConnection::~WebRtcPeerConnection()
{
    stop();
}

WebRtcPeerConnection::CallbackGuard::CallbackGuard(WebRtcPeerConnection* owner)
    : owner_(owner)
{
    if (owner_) {
        std::lock_guard lock(owner_->callback_mutex_);
        ++owner_->callbacks_inflight_;
    }
}

WebRtcPeerConnection::CallbackGuard::~CallbackGuard()
{
    if (!owner_) {
        return;
    }
    {
        std::lock_guard lock(owner_->callback_mutex_);
        if (owner_->callbacks_inflight_ > 0) {
            --owner_->callbacks_inflight_;
        }
    }
    owner_->callback_cv_.notify_all();
}

void WebRtcPeerConnection::wait_for_callbacks()
{
    std::unique_lock lock(callback_mutex_);
    callback_cv_.wait(lock, [this] { return callbacks_inflight_ == 0; });
}

void WebRtcPeerConnection::set_local_candidate_handler(LocalCandidateHandler handler)
{
    std::lock_guard lock(mutex_);
    on_candidate_ = std::move(handler);
}

void WebRtcPeerConnection::set_local_sdp_handler(LocalSdpHandler handler)
{
    std::lock_guard lock(mutex_);
    on_sdp_ = std::move(handler);
}

void WebRtcPeerConnection::set_keyframe_handler(KeyframeHandler handler)
{
    std::lock_guard lock(mutex_);
    on_keyframe_ = std::move(handler);
}

void WebRtcPeerConnection::set_state_handler(StateHandler handler)
{
    std::lock_guard lock(mutex_);
    on_state_ = std::move(handler);
}

void WebRtcPeerConnection::set_remote_audio_handler(RemoteAudioHandler handler)
{
    std::lock_guard lock(mutex_);
    on_remote_audio_ = std::move(handler);
}

#if defined(IPC_MINI_ENABLE_KVS) && IPC_MINI_ENABLE_KVS

bool WebRtcPeerConnection::start()
{
    std::lock_guard lock(mutex_);
    if (started_) {
        return true;
    }
    // Init once for process lifetime. Per-session deinitKvsWebRtc() routinely
    // hangs on musl/usrsctp during Ctrl+C (freeSctpSession / iceAgentShutdown).
    static std::once_flag kvs_once;
    static STATUS kvs_init_status = STATUS_SUCCESS;
    std::call_once(kvs_once, [] {
        kvs_init_status = initKvsWebRtc();
        if (STATUS_FAILED(kvs_init_status)) {
            std::fprintf(stderr, "[kvs] initKvsWebRtc failed 0x%08x\n",
                         kvs_init_status);
        }
    });
    STATUS status = kvs_init_status;
    if (STATUS_FAILED(status)) {
        return false;
    }

    RtcConfiguration cfg {};
    cfg.iceTransportPolicy = ICE_TRANSPORT_POLICY_ALL;
    cfg.kvsRtcConfiguration.disableSenderSideBandwidthEstimation =
        config_.disable_twcc ? TRUE : FALSE;

    UINT32 ice_count = 0;
    for (const auto& ice : config_.ice_servers) {
        if (ice_count >= MAX_ICE_SERVERS_COUNT) {
            break;
        }
        auto& dst = cfg.iceServers[ice_count++];
        SNPRINTF(dst.urls, ARRAY_SIZE(dst.urls), "%s", ice.urls.c_str());
        if (!ice.username.empty()) {
            SNPRINTF(dst.username, ARRAY_SIZE(dst.username), "%s",
                     ice.username.c_str());
        }
        if (!ice.credential.empty()) {
            SNPRINTF(dst.credential, ARRAY_SIZE(dst.credential), "%s",
                     ice.credential.c_str());
        }
    }

    PRtcPeerConnection pc = nullptr;
    status = createPeerConnection(&cfg, &pc);
    if (STATUS_FAILED(status) || !pc) {
        std::fprintf(stderr, "[kvs] createPeerConnection failed 0x%08x\n", status);
        return false;
    }
    peer_connection_ = pc;

    peerConnectionOnIceCandidate(
        pc, reinterpret_cast<UINT64>(this),
        [](UINT64 custom, PCHAR candidateJson) {
            auto* self = reinterpret_cast<WebRtcPeerConnection*>(custom);
            if (!self) {
                return;
            }
            WebRtcPeerConnection::CallbackGuard guard(self);
            static std::atomic<unsigned> ice_n{0};
            // NULL means gathering complete (KVS convention).
            if (!candidateJson) {
                std::fprintf(stderr,
                             "[kvs] ice gathering done (local candidates=%u)\n",
                             ice_n.exchange(0));
                return;
            }
            ++ice_n;
            LocalCandidateHandler handler;
            {
                std::lock_guard lk(self->mutex_);
                handler = self->on_candidate_;
            }
            if (handler) {
                handler(candidateJson);
            }
        });
    peerConnectionOnConnectionStateChange(
        pc, reinterpret_cast<UINT64>(this),
        [](UINT64 custom, RTC_PEER_CONNECTION_STATE state) {
            auto* self = reinterpret_cast<WebRtcPeerConnection*>(custom);
            if (!self) {
                return;
            }
            WebRtcPeerConnection::CallbackGuard guard(self);
            const char* name = "unknown";
            switch (state) {
            case RTC_PEER_CONNECTION_STATE_NEW: name = "new"; break;
            case RTC_PEER_CONNECTION_STATE_CONNECTING: name = "connecting"; break;
            case RTC_PEER_CONNECTION_STATE_CONNECTED:
                name = "connected";
                self->media_ready_ = true;
                break;
            case RTC_PEER_CONNECTION_STATE_DISCONNECTED: name = "disconnected"; break;
            case RTC_PEER_CONNECTION_STATE_FAILED: name = "failed"; break;
            case RTC_PEER_CONNECTION_STATE_CLOSED:
                name = "closed";
                self->media_ready_ = false;
                break;
            default: break;
            }
            StateHandler handler;
            {
                std::lock_guard lk(self->mutex_);
                handler = self->on_state_;
            }
            if (handler) {
                handler(name);
            }
        });

    addSupportedCodec(
        pc,
        RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE);
    addSupportedCodec(pc, RTC_CODEC_MULAW);

    RtcMediaStreamTrack track {};
    track.kind = MEDIA_STREAM_TRACK_KIND_VIDEO;
    track.codec =
        RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE;
    STRNCPY(track.streamId, "ipc-mini", ARRAY_SIZE(track.streamId));
    STRNCPY(track.trackId, "video", ARRAY_SIZE(track.trackId));

    RtcRtpTransceiverInit init {};
    init.direction = RTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
    PRtcRtpTransceiver xcvr = nullptr;
    status = addTransceiver(pc, &track, &init, &xcvr);
    if (STATUS_FAILED(status) || !xcvr) {
        std::fprintf(stderr, "[kvs] addTransceiver video failed 0x%08x\n",
                     status);
        return false;
    }
    video_transceiver_ = xcvr;
    configureTransceiverRollingBuffer(
        xcvr, &track,
        static_cast<UINT64>(config_.rolling_buffer_duration_sec),
        static_cast<UINT64>(config_.expected_bitrate_bps));

    transceiverOnPictureLoss(
        xcvr, reinterpret_cast<UINT64>(this),
        [](UINT64 custom) {
            auto* self = reinterpret_cast<WebRtcPeerConnection*>(custom);
            if (!self) {
                return;
            }
            WebRtcPeerConnection::CallbackGuard guard(self);
            KeyframeHandler handler;
            {
                std::lock_guard lk(self->mutex_);
                handler = self->on_keyframe_;
            }
            if (handler) {
                handler();
            }
        });

    RtcMediaStreamTrack audio_track {};
    audio_track.kind = MEDIA_STREAM_TRACK_KIND_AUDIO;
    audio_track.codec = RTC_CODEC_MULAW;
    STRNCPY(audio_track.streamId, "ipc-mini", ARRAY_SIZE(audio_track.streamId));
    STRNCPY(audio_track.trackId, "audio", ARRAY_SIZE(audio_track.trackId));
    RtcRtpTransceiverInit audio_init {};
    audio_init.direction = RTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV;
    PRtcRtpTransceiver audio_xcvr = nullptr;
    status = addTransceiver(pc, &audio_track, &audio_init, &audio_xcvr);
    if (STATUS_FAILED(status) || !audio_xcvr) {
        std::fprintf(stderr, "[kvs] addTransceiver audio failed 0x%08x\n",
                     status);
        return false;
    }
    audio_transceiver_ = audio_xcvr;
    transceiverOnFrame(
        audio_xcvr, reinterpret_cast<UINT64>(this),
        [](UINT64 custom, PFrame frame) {
            auto* self = reinterpret_cast<WebRtcPeerConnection*>(custom);
            if (!self || !frame || !frame->frameData || frame->size == 0) {
                return;
            }
            WebRtcPeerConnection::CallbackGuard guard(self);
            RemoteAudioHandler handler;
            {
                std::lock_guard lk(self->mutex_);
                handler = self->on_remote_audio_;
            }
            if (handler) {
                static std::atomic<unsigned> rx_n{0};
                if (++rx_n == 1) {
                    std::fprintf(stderr,
                                 "[kvs] remote audio started size=%u\n",
                                 frame->size);
                }
                handler(frame->frameData, static_cast<size_t>(frame->size));
            }
        });

    // Answerer must NOT createDataChannel before the remote offer — that adds an
    // application m-line the offer never had and breaks createAnswer / answer SDP.
    // Viewer creates the channel; we accept it here and send detections on it.
    if (config_.enable_datachannel) {
        peerConnectionOnDataChannel(
            pc, reinterpret_cast<UINT64>(this),
            [](UINT64 custom, PRtcDataChannel dc) {
                auto* self = reinterpret_cast<WebRtcPeerConnection*>(custom);
                if (!self || !dc) {
                    return;
                }
                WebRtcPeerConnection::CallbackGuard guard(self);
                {
                    std::lock_guard lock(self->mutex_);
                    self->data_channel_ = dc;
                    self->dc_open_ = true;
                }
                // KVS answerer path (onSctpSessionDataChannelOpen) marks the
                // channel OPEN and invokes onDataChannel — it never fires
                // dataChannelOnOpen. Waiting on that callback left dc_open_=0
                // forever while the browser already showed "dc open".
                dataChannelOnOpen(dc, custom,
                                  [](UINT64 c, PRtcDataChannel) {
                                      auto* s =
                                          reinterpret_cast<WebRtcPeerConnection*>(c);
                                      if (s) {
                                          WebRtcPeerConnection::CallbackGuard guard(s);
                                          std::lock_guard lock(s->mutex_);
                                          if (s->peer_connection_) {
                                              s->dc_open_ = true;
                                          }
                                          std::fprintf(stderr,
                                                       "[kvs] datachannel "
                                                       "onOpen\n");
                                      }
                                  });
                std::fprintf(stderr,
                             "[kvs] datachannel received from viewer (ready)\n");
            });
    }

    started_ = true;
    return true;
}

void WebRtcPeerConnection::deactivate()
{
    std::lock_guard lock(mutex_);
    media_ready_ = false;
    dc_open_ = false;
    on_candidate_ = nullptr;
    on_sdp_ = nullptr;
    on_keyframe_ = nullptr;
    on_state_ = nullptr;
    on_remote_audio_ = nullptr;
}

void WebRtcPeerConnection::stop()
{
    // Never hold mutex_ across close/free — ICE/SCTP workers may callback.
    // Do NOT call deinitKvsWebRtc() here: it blocks indefinitely on this board
    // once DataChannel/usrsctp was used (symptom: Ctrl+C → stuck, need Ctrl+Z).
    deactivate();
    wait_for_callbacks();
    PRtcPeerConnection pc = nullptr;
    {
        std::lock_guard lock(mutex_);
        media_ready_ = false;
        dc_open_ = false;
        on_candidate_ = nullptr;
        on_sdp_ = nullptr;
        on_keyframe_ = nullptr;
        on_state_ = nullptr;
        on_remote_audio_ = nullptr;
        if (peer_connection_) {
            pc = reinterpret_cast<PRtcPeerConnection>(peer_connection_);
            peer_connection_ = nullptr;
        }
        video_transceiver_ = nullptr;
        audio_transceiver_ = nullptr;
        data_channel_ = nullptr;
        audio_pts_100ns_ = 0;
        started_ = false;
    }
    if (!pc) {
        return;
    }
    std::fprintf(stderr, "[kvs] closePeerConnection...\n");
    (void)closePeerConnection(pc);
    wait_for_callbacks();
    std::fprintf(stderr, "[kvs] freePeerConnection...\n");
    (void)freePeerConnection(&pc);
    std::fprintf(stderr, "[kvs] peer connection released\n");
}

bool WebRtcPeerConnection::handle_remote_offer(const std::string& sdp)
{
    // Do NOT hold mutex_ across KVS SDP/ICE APIs: setLocalDescription starts
    // gathering and may invoke onIceCandidate on the same thread. Taking
    // mutex_ again there deadlocks — board stuck after ICE gather, no answer.
    PRtcPeerConnection pc = nullptr;
    LocalSdpHandler sdp_handler;
    {
        std::lock_guard lock(mutex_);
        if (!peer_connection_) {
            return false;
        }
        pc = reinterpret_cast<PRtcPeerConnection>(peer_connection_);
        sdp_handler = on_sdp_;
    }

    RtcSessionDescriptionInit remote {};
    remote.type = SDP_TYPE_OFFER;
    STRNCPY(remote.sdp, sdp.c_str(), MAX_SESSION_DESCRIPTION_INIT_SDP_LEN);

    std::fprintf(stderr, "[kvs] setRemoteDescription...\n");
    STATUS status = setRemoteDescription(pc, &remote);
    if (STATUS_FAILED(status)) {
        std::fprintf(stderr,
                     "[kvs] setRemoteDescription failed 0x%08x (sdp_len=%zu)\n",
                     status, sdp.size());
        return false;
    }
    RtcSessionDescriptionInit answer {};
    std::fprintf(stderr, "[kvs] createAnswer...\n");
    status = createAnswer(pc, &answer);
    if (STATUS_FAILED(status)) {
        std::fprintf(stderr, "[kvs] createAnswer failed 0x%08x\n", status);
        return false;
    }
    std::fprintf(stderr, "[kvs] setLocalDescription...\n");
    status = setLocalDescription(pc, &answer);
    if (STATUS_FAILED(status)) {
        std::fprintf(stderr, "[kvs] setLocalDescription failed 0x%08x\n",
                     status);
        return false;
    }
    if (sdp_handler) {
        std::fprintf(stderr, "[kvs] sending answer sdp_len=%zu\n",
                     std::strlen(answer.sdp));
        sdp_handler("answer", answer.sdp);
    } else {
        std::fprintf(stderr, "[kvs] answer ready but no sdp handler\n");
        return false;
    }
    return true;
}

bool WebRtcPeerConnection::handle_remote_answer(const std::string& sdp)
{
    PRtcPeerConnection pc = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (!peer_connection_) {
            return false;
        }
        pc = reinterpret_cast<PRtcPeerConnection>(peer_connection_);
    }
    RtcSessionDescriptionInit remote {};
    remote.type = SDP_TYPE_ANSWER;
    STRNCPY(remote.sdp, sdp.c_str(), MAX_SESSION_DESCRIPTION_INIT_SDP_LEN);
    return STATUS_SUCCEEDED(setRemoteDescription(pc, &remote));
}

bool WebRtcPeerConnection::handle_remote_candidate(const std::string& candidate,
                                             const std::string& sdp_mid,
                                             int sdp_mline_index)
{
    PRtcPeerConnection pc = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (!peer_connection_) {
            return false;
        }
        pc = reinterpret_cast<PRtcPeerConnection>(peer_connection_);
    }
    RtcIceCandidateInit ice {};
    SNPRINTF(ice.candidate, ARRAY_SIZE(ice.candidate), "%s", candidate.c_str());
    (void)sdp_mid;
    (void)sdp_mline_index;
    return STATUS_SUCCEEDED(addIceCandidate(pc, ice.candidate));
}

bool WebRtcPeerConnection::write_video_frame(
    const std::shared_ptr<const ipc_mini::media::MediaFrame>& frame)
{
    if (!frame || !media_ready_.load() || !video_transceiver_) {
        return false;
    }
    Frame kvs_frame {};
    kvs_frame.version = FRAME_CURRENT_VERSION;
    kvs_frame.frameData = const_cast<PBYTE>(frame->data());
    kvs_frame.size = static_cast<UINT32>(frame->size());
    kvs_frame.presentationTs =
        static_cast<UINT64>(frame->pts_ms()) * 10000ULL; // ms -> 100ns
    kvs_frame.decodingTs = kvs_frame.presentationTs;
    kvs_frame.flags =
        frame->keyframe() ? FRAME_FLAG_KEY_FRAME : FRAME_FLAG_NONE;
    auto* xcvr = reinterpret_cast<PRtcRtpTransceiver>(video_transceiver_);
    const STATUS status = writeFrame(xcvr, &kvs_frame);
    if (STATUS_SUCCEEDED(status) || status == STATUS_SRTP_NOT_READY_YET) {
        static std::atomic<unsigned> vf_ok{0};
        if (++vf_ok == 1) {
            std::fprintf(stderr,
                         "[kvs] video uplink started size=%u key=%d\n",
                         kvs_frame.size, frame->keyframe() ? 1 : 0);
        }
        return true;
    }
    static std::atomic<unsigned> wf_fail{0};
    const unsigned n = ++wf_fail;
    if (n == 1 || (n % 100) == 0) {
        std::fprintf(stderr,
                     "[kvs] video uplink fail status=0x%08x count=%u size=%u\n",
                     status, n, kvs_frame.size);
    }
    return false;
}

bool WebRtcPeerConnection::write_audio_frame(
    const std::shared_ptr<const ipc_mini::media::MediaFrame>& frame)
{
    if (!frame || frame->type() != ipc_mini::media::MediaType::Audio ||
        !media_ready_.load() || !audio_transceiver_) {
        return false;
    }
    if (frame->codec() != ipc_mini::media::Codec::G711U || frame->size() == 0) {
        return false;
    }
    /*
     * Drive RTP timestamps from sample count (1 byte = 1 sample @ 8kHz).
     * Encoder PTS from HiSilicon AENC is often irregular and produces
     * periodic click/tick artifacts in the browser jitter buffer.
     * Duration in 100ns: samples * 1e7 / 8000 = samples * 1250.
     */
    const UINT64 pts = audio_pts_100ns_;
    audio_pts_100ns_ += static_cast<UINT64>(frame->size()) * 1250ULL;

    Frame kvs_frame {};
    kvs_frame.version = FRAME_CURRENT_VERSION;
    kvs_frame.frameData = const_cast<PBYTE>(frame->data());
    kvs_frame.size = static_cast<UINT32>(frame->size());
    kvs_frame.presentationTs = pts;
    kvs_frame.decodingTs = pts;
    kvs_frame.duration = static_cast<UINT64>(frame->size()) * 1250ULL;
    kvs_frame.flags = FRAME_FLAG_NONE;
    auto* xcvr = reinterpret_cast<PRtcRtpTransceiver>(audio_transceiver_);
    const STATUS status = writeFrame(xcvr, &kvs_frame);
    if (STATUS_SUCCEEDED(status) || status == STATUS_SRTP_NOT_READY_YET) {
        static std::atomic<unsigned> af_ok{0};
        if (++af_ok == 1) {
            std::fprintf(stderr, "[kvs] audio uplink started size=%u\n",
                         kvs_frame.size);
        }
        return true;
    }
    static std::atomic<unsigned> af_fail{0};
    const unsigned n = ++af_fail;
    if (n == 1 || (n % 100) == 0) {
        std::fprintf(stderr,
                     "[kvs] audio uplink fail status=0x%08x count=%u size=%u\n",
                     status, n, kvs_frame.size);
    }
    return false;
}

bool WebRtcPeerConnection::send_detections(
    const ipc_mini::media::DetectionResult& detections)
{
    PRtcDataChannel dc = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (dc_open_.load() && data_channel_) {
            dc = reinterpret_cast<PRtcDataChannel>(data_channel_);
        }
    }
    if (!dc) {
        static std::atomic<unsigned> drop_n{0};
        const unsigned n = ++drop_n;
        if (n == 1 || (n % 200) == 0) {
            std::fprintf(stderr,
                         "[kvs] detection skipped (datachannel not ready) "
                         "count=%u objs=%zu\n",
                         n, detections.boxes.size());
        }
        return false;
    }
    // Compact JSON without jsoncpp dependency in hot path.
    std::string json = "{\"pts_ms\":" + std::to_string(detections.pts_ms) +
        ",\"w\":" + std::to_string(detections.frame_width) +
        ",\"h\":" + std::to_string(detections.frame_height) + ",\"objs\":[";
    for (size_t i = 0; i < detections.boxes.size(); ++i) {
        const auto& b = detections.boxes[i];
        if (i) {
            json.push_back(',');
        }
        json += "{\"cls\":" + std::to_string(b.class_id) +
            ",\"s\":" + std::to_string(b.score) +
            ",\"x\":" + std::to_string(b.x) + ",\"y\":" + std::to_string(b.y) +
            ",\"bw\":" + std::to_string(b.w) + ",\"bh\":" +
            std::to_string(b.h) + "}";
    }
    json += "]}";
    // Send as text (isBinary=FALSE) so browser JSON.parse(m.data) works.
    const STATUS status = dataChannelSend(
        dc, FALSE, reinterpret_cast<PBYTE>(json.data()),
        static_cast<UINT32>(json.size()));
    static std::atomic<unsigned> send_ok{0};
    static std::atomic<unsigned> send_fail{0};
    static std::atomic<unsigned> hit_ok{0};
    if (STATUS_SUCCEEDED(status)) {
        const unsigned n = ++send_ok;
        const size_t objs = detections.boxes.size();
        if (n == 1) {
            std::fprintf(stderr,
                         "[kvs] detection channel ready (first send %zu bytes)\n",
                         json.size());
        } else if (objs > 0 && ++hit_ok == 1) {
            std::fprintf(stderr,
                         "[kvs] first detection objs=%zu bytes=%zu\n",
                         objs, json.size());
        }
        return true;
    }
    const unsigned n = ++send_fail;
    if (n == 1 || (n % 100) == 0) {
        std::fprintf(stderr,
                     "[kvs] detection send fail status=0x%08x count=%u\n",
                     status, n);
    }
    return false;
}

#else // !IPC_MINI_ENABLE_KVS

bool WebRtcPeerConnection::start()
{
    std::fprintf(stderr,
                 "[kvs] stub peer connection started (build with IPC_MINI_ENABLE_KVS=1 "
                 "after cross-compiling amazon-kinesis-video-streams-webrtc-sdk-c)\n");
    started_ = true;
    return true;
}

void WebRtcPeerConnection::deactivate()
{
    media_ready_ = false;
    dc_open_ = false;
    std::lock_guard lock(mutex_);
    on_candidate_ = nullptr;
    on_sdp_ = nullptr;
    on_keyframe_ = nullptr;
    on_state_ = nullptr;
    on_remote_audio_ = nullptr;
}

void WebRtcPeerConnection::stop()
{
    deactivate();
    wait_for_callbacks();
    media_ready_ = false;
    dc_open_ = false;
    started_ = false;
}

bool WebRtcPeerConnection::handle_remote_offer(const std::string& sdp)
{
    std::fprintf(stderr, "[kvs] stub got offer (%zu bytes) — KVS not linked\n",
                 sdp.size());
    LocalSdpHandler handler;
    {
        std::lock_guard lock(mutex_);
        handler = on_sdp_;
    }
    if (handler) {
        handler("answer", "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n");
    }
    return false;
}

bool WebRtcPeerConnection::handle_remote_answer(const std::string&)
{
    return false;
}

bool WebRtcPeerConnection::handle_remote_candidate(const std::string&,
                                             const std::string&, int)
{
    return false;
}

bool WebRtcPeerConnection::write_video_frame(
    const std::shared_ptr<const ipc_mini::media::MediaFrame>&)
{
    return false;
}

bool WebRtcPeerConnection::write_audio_frame(
    const std::shared_ptr<const ipc_mini::media::MediaFrame>&)
{
    return false;
}

bool WebRtcPeerConnection::send_detections(const ipc_mini::media::DetectionResult&)
{
    return false;
}

#endif

} // namespace ipc_mini::webrtc_net
