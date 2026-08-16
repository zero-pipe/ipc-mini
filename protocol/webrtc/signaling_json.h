#pragma once

#include <json/json.h>
#include <string>

namespace zero_mini::webrtc_net {

/**
 * JSON helpers for signaling wire text and PeerConnection callback payloads.
 * Not a protocol codec — join/answer encoding stays in SignalingClient.
 */
std::string json_escape_string(const std::string& value);

bool parse_json_object(const std::string& text,
                       Json::Value& object,
                       std::string& error);

bool get_required_string(const Json::Value& object,
                         const char* field,
                         std::string& value,
                         std::string& error);

bool get_optional_string(const Json::Value& object,
                         const char* field,
                         std::string& value,
                         std::string& error);

bool get_optional_int(const Json::Value& object,
                      const char* field,
                      int& value,
                      std::string& error);

} // namespace zero_mini::webrtc_net
