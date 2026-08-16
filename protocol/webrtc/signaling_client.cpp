#include "signaling_client.h"
#include "signaling_json.h"

#include <ztk/util/timer.h>

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace zero_mini::webrtc_net {
namespace {


std::string url_encode_query_value(const std::string& value)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (unsigned char c : value) {
        const bool safe =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~';
        if (safe) {
            encoded.push_back(static_cast<char>(c));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[c >> 4]);
            encoded.push_back(hex[c & 0x0f]);
        }
    }
    return encoded;
}

std::string endpoint_without_query_credentials(const std::string& url)
{
    const auto query = url.find('?');
    return query == std::string::npos ? url : url.substr(0, query);
}

} // namespace

SignalingClient::SignalingClient(SignalingConfig config, ztk_poller* poller)
    : config_(std::move(config)), ws_(poller), poller_(poller)
{
}

SignalingClient::~SignalingClient()
{
    stop();
}

void SignalingClient::set_handler(JsonHandler handler)
{
    std::lock_guard lock(mutex_);
    handler_ = std::move(handler);
}

std::string SignalingClient::client_id() const
{
    std::lock_guard lock(mutex_);
    return assigned_id_.empty() ? config_.client_id : assigned_id_;
}

std::string SignalingClient::build_url() const
{
    std::string url = config_.url;
    if (!config_.token.empty()) {
        url += (url.find('?') == std::string::npos ? "?" : "&");
        url += "token=" + url_encode_query_value(config_.token);
    }
    return url;
}

bool SignalingClient::start()
{
    stopping_ = false;
    ws_.set_message_handler(
        [this](const std::string& text) { on_message(text); });
    ws_.set_state_handler(
        [this](bool connected) { on_ws_state(connected); });
    if (!ws_.connect(build_url())) {
        std::fprintf(stderr, "[signaling] connect failed: %s\n",
                     endpoint_without_query_credentials(config_.url).c_str());
        schedule_reconnect();
    }
    return true;
}

void SignalingClient::stop()
{
    stopping_ = true;
    ztk_poller_timer* timer = nullptr;
    {
        std::lock_guard lock(mutex_);
        handler_ = nullptr;
        timer = reconnect_timer_;
        reconnect_timer_ = nullptr;
    }
    if (timer) {
        ztk_poller_timer_cancel(timer);
    }
    if (ws_.connected()) {
        (void)send_raw(R"({"type":"leave"})");
    }
    ws_.close();
}

void SignalingClient::on_ws_state(bool connected)
{
    if (stopping_.load()) {
        return;
    }
    if (connected) {
        reconnect_attempt_ = 0;
        if (!send_join()) {
            std::fprintf(stderr, "[signaling] join send failed\n");
            ws_.close();
            on_ws_state(false);
        }
        return;
    }

    JsonHandler handler;
    {
        std::lock_guard lock(mutex_);
        handler = handler_;
    }
    if (handler) {
        handler("signaling-disconnected",
                R"({"type":"signaling-disconnected"})");
    }
    schedule_reconnect();
}

void SignalingClient::schedule_reconnect()
{
    if (stopping_.load() || !poller_) {
        return;
    }
    const unsigned attempt = std::min(reconnect_attempt_++, 5u);
    const uint64_t delay_ms = std::min<uint64_t>(
        30000, 1000ULL << attempt);
    std::lock_guard lock(mutex_);
    if (!reconnect_timer_) {
        reconnect_timer_ = ztk_poller_do_delay(
            poller_, delay_ms, &SignalingClient::reconnect_timer_cb, this);
    }
}

uint64_t SignalingClient::reconnect_timer_cb(void* user)
{
    auto* self = static_cast<SignalingClient*>(user);
    if (!self) {
        return 0;
    }
    {
        std::lock_guard lock(self->mutex_);
        self->reconnect_timer_ = nullptr;
    }
    if (self->stopping_.load()) {
        return 0;
    }
    std::fprintf(stderr, "[signaling] reconnect attempt=%u\n",
                 self->reconnect_attempt_);
    if (!self->ws_.connect(self->build_url())) {
        self->schedule_reconnect();
    }
    return 0;
}

bool SignalingClient::send_raw(const std::string& json)
{
    return ws_.send_text(json);
}

bool SignalingClient::send_join()
{
    std::ostringstream oss;
    oss << "{\"type\":\"join\",\"room\":\"" << json_escape_string(config_.room)
        << "\",\"role\":\"" << json_escape_string(config_.role) << "\"";
    if (!config_.client_id.empty()) {
        oss << ",\"clientId\":\"" << json_escape_string(config_.client_id) << "\"";
    }
    oss << "}";
    return send_raw(oss.str());
}

bool SignalingClient::send_answer(const std::string& sdp, const std::string& to)
{
    std::ostringstream oss;
    oss << "{\"type\":\"answer\",\"sdp\":\"" << json_escape_string(sdp) << "\"";
    if (!to.empty()) {
        oss << ",\"to\":\"" << json_escape_string(to) << "\"";
    }
    oss << "}";
    return send_raw(oss.str());
}

bool SignalingClient::send_candidate(const std::string& candidate,
                                     const std::string& sdp_mid,
                                     int sdp_mline_index,
                                     const std::string& to)
{
    std::ostringstream oss;
    oss << "{\"type\":\"candidate\",\"candidate\":\"" << json_escape_string(candidate)
        << "\",\"sdpMid\":\"" << json_escape_string(sdp_mid)
        << "\",\"sdpMLineIndex\":" << sdp_mline_index;
    if (!to.empty()) {
        oss << ",\"to\":\"" << json_escape_string(to) << "\"";
    }
    oss << "}";
    return send_raw(oss.str());
}

void SignalingClient::on_message(const std::string& text)
{
    Json::Value message;
    std::string parse_error;
    if (!parse_json_object(text, message, parse_error)) {
        std::fprintf(stderr, "[signaling] invalid message: %s\n",
                     parse_error.c_str());
        return;
    }
    std::string type;
    if (!get_required_string(message, "type", type, parse_error)) {
        std::fprintf(stderr, "[signaling] invalid message type: %s\n",
                     parse_error.c_str());
        return;
    }
    if (type == "joined") {
        std::string id;
        if (get_optional_string(message, "clientId", id, parse_error)) {
            std::lock_guard lock(mutex_);
            if (!id.empty()) assigned_id_ = id;
        }
    }
    JsonHandler handler;
    {
        std::lock_guard lock(mutex_);
        handler = handler_;
    }
    if (handler) {
        handler(type, text);
    }
}

} // namespace zero_mini::webrtc_net
