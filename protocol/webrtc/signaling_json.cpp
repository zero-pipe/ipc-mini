#include "signaling_json.h"

#include <memory>

namespace ipc_mini::webrtc_net {
namespace {

void append_hex_escape(std::string& output, unsigned char value)
{
    static constexpr char hex[] = "0123456789abcdef";
    output += "\\u00";
    output.push_back(hex[value >> 4]);
    output.push_back(hex[value & 0x0f]);
}

bool get_member(const Json::Value& object, const char* field,
                const Json::Value*& value, std::string& error)
{
    if (!object.isObject()) {
        error = "message must be a JSON object";
        return false;
    }
    if (!object.isMember(field)) {
        error = std::string("missing field: ") + field;
        return false;
    }
    value = &object[field];
    return true;
}

} // namespace

std::string json_escape_string(const std::string& value)
{
    std::string output;
    output.reserve(value.size() + 8);
    for (unsigned char character : value) {
        switch (character) {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20) {
                append_hex_escape(output, character);
            } else {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return output;
}

bool parse_json_object(const std::string& text, Json::Value& object,
                       std::string& error)
{
    if (text.empty() || text.size() > 512 * 1024) {
        error = "message size is invalid";
        return false;
    }
    Json::Reader reader;
    if (!reader.parse(text, object, false) || !object.isObject()) {
        error = reader.getFormatedErrorMessages();
        if (error.empty()) error = "message must be a JSON object";
        return false;
    }
    return true;
}

bool get_required_string(const Json::Value& object, const char* field,
                         std::string& value, std::string& error)
{
    const Json::Value* member = nullptr;
    if (!get_member(object, field, member, error)) return false;
    if (!member->isString() || member->asString().empty()) {
        error = std::string("field must be a non-empty string: ") + field;
        return false;
    }
    value = member->asString();
    return true;
}

bool get_optional_string(const Json::Value& object, const char* field,
                         std::string& value, std::string& error)
{
    if (!object.isObject()) {
        error = "message must be a JSON object";
        return false;
    }
    if (!object.isMember(field)) {
        value.clear();
        return true;
    }
    const Json::Value& member = object[field];
    if (!member.isString()) {
        error = std::string("field must be a string: ") + field;
        return false;
    }
    value = member.asString();
    return true;
}

bool get_optional_int(const Json::Value& object, const char* field,
                      int& value, std::string& error)
{
    if (!object.isObject()) {
        error = "message must be a JSON object";
        return false;
    }
    if (!object.isMember(field)) return true;
    const Json::Value& member = object[field];
    if (!member.isInt()) {
        error = std::string("field must be an integer: ") + field;
        return false;
    }
    value = member.asInt();
    return true;
}

} // namespace ipc_mini::webrtc_net
