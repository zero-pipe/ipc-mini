#include "webrtc_plugin_impl.h"

#include <cstdio>

namespace zero_ipc::protocol {
using zero_mini::webrtc_net::get_optional_string;
using zero_mini::webrtc_net::get_required_string;
using zero_mini::webrtc_net::parse_json_object;

void WebRtcPlugin::Impl::on_signaling_message(const std::string& type,
                                              const std::string& json)
{
    if (!running.load()) {
        return;
    }
    Json::Value message;
    std::string parse_error;
    if (!parse_json_object(json, message, parse_error)) {
        std::fprintf(stderr, "[webrtc] invalid signaling message: %s\n",
                     parse_error.c_str());
        return;
    }
    if (type == "peer-joined") {
        std::string role;
        std::string id;
        if (!get_optional_string(message, "role", role, parse_error) ||
            !get_optional_string(message, "clientId", id, parse_error)) {
            return;
        }
        if (role == "viewer" && !id.empty()) {
            std::fprintf(stderr, "[webrtc] viewer joined %s\n", id.c_str());
        }
        return;
    }
    if (type == "peer-left") {
        on_peer_left(message, parse_error);
        return;
    }
    if (type == "offer") {
        on_offer(message, parse_error);
        return;
    }
    if (type == "candidate") {
        on_candidate_message(message, parse_error);
        return;
    }
    if (type == "signaling-disconnected") {
        on_signaling_disconnected();
        return;
    }
    if (type == "error") {
        std::fprintf(stderr, "[webrtc] signaling error: %s\n", json.c_str());
    }
}

void WebRtcPlugin::Impl::on_offer(const Json::Value& message,
                                  std::string& parse_error)
{
    std::string sdp;
    std::string viewer_id;
    if (!get_required_string(message, "sdp", sdp, parse_error) ||
        !get_required_string(message, "from", viewer_id, parse_error)) {
        return;
    }
    if (sdp.find("ice-ufrag") == std::string::npos ||
        sdp.find("ice-pwd") == std::string::npos) {
        std::fprintf(stderr, "[webrtc] invalid offer viewer=%s len=%zu\n",
                     viewer_id.c_str(), sdp.size());
        return;
    }
    if (!create_viewer(viewer_id, sdp)) {
        std::fprintf(stderr, "[webrtc] setup failed viewer=%s\n",
                     viewer_id.c_str());
    }
}

void WebRtcPlugin::Impl::on_candidate_message(const Json::Value& message,
                                              std::string& parse_error)
{
    std::string from;
    std::string candidate;
    std::string sdp_mid;
    if (!get_required_string(message, "from", from, parse_error) ||
        !get_required_string(message, "candidate", candidate, parse_error) ||
        !get_optional_string(message, "sdpMid", sdp_mid, parse_error)) {
        return;
    }
    apply_remote_candidate(from, candidate, sdp_mid);
}

void WebRtcPlugin::Impl::on_peer_left(const Json::Value& message,
                                      std::string& parse_error)
{
    std::string role;
    std::string id;
    if (!get_optional_string(message, "role", role, parse_error) ||
        !get_optional_string(message, "clientId", id, parse_error)) {
        return;
    }
    if (role == "viewer" && !id.empty()) {
        remove_viewer(id, "peer-left");
    }
}

void WebRtcPlugin::Impl::on_signaling_disconnected()
{
    remove_all_viewers("signaling-disconnected");
}

} // namespace zero_ipc::protocol
