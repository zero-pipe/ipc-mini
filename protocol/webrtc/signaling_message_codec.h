#pragma once

#include <json/json.h>
#include <string>

namespace zero_mini::webrtc_net {

/**
 * Shared JSON boundary for signaling and KVS callback messages.
 * The wire format remains unchanged; this only centralizes escaping and
 * typed field validation.
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
