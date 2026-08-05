#include "config/runtime_config.h"

#include <json/json.h>
#include <cstdio>
#include <string>
#include <utility>

namespace zero_ipc::config {
namespace {

constexpr long kMaxConfigBytes = 64 * 1024;

bool read_json(const std::string& path, Json::Value& root, std::string& error)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        error = "cannot open " + path;
        return false;
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        error = "cannot seek " + path;
        return false;
    }
    const long size = std::ftell(file);
    if (size <= 0 || size > kMaxConfigBytes ||
        std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        error = "invalid config size: " + path;
        return false;
    }

    std::string text(static_cast<std::size_t>(size), '\0');
    const std::size_t read =
        std::fread(text.data(), 1, text.size(), file);
    std::fclose(file);
    if (read != text.size()) {
        error = "cannot read " + path;
        return false;
    }

    Json::Reader reader;
    if (!reader.parse(text, root, false)) {
        error = "invalid JSON in " + path + ": " +
                reader.getFormatedErrorMessages();
        return false;
    }
    return true;
}

std::string path_join(const std::string& directory, const char* file)
{
    if (!directory.empty() && directory.back() == '/') {
        return directory + file;
    }
    return directory + "/" + file;
}

bool positive(int value)
{
    return value > 0;
}

bool required_string(const Json::Value& object, const char* file,
                    const char* field, std::string& value,
                    std::string& error)
{
    if (!object.isObject() || !object.isMember(field) ||
        !object[field].isString() || object[field].asString().empty()) {
        error = std::string(file) + ":" + field +
                " must be a non-empty string";
        return false;
    }
    value = object[field].asString();
    return true;
}

bool required_int(const Json::Value& object, const char* file,
                  const char* field, int& value, std::string& error)
{
    if (!object.isObject() || !object.isMember(field) ||
        !object[field].isInt()) {
        error = std::string(file) + ":" + field + " must be an integer";
        return false;
    }
    value = object[field].asInt();
    return true;
}

bool optional_int(const Json::Value& object, const char* file,
                  const char* field, int default_value, int& value,
                  std::string& error)
{
    if (!object.isObject() || !object.isMember(field)) {
        value = default_value;
        return true;
    }
    return required_int(object, file, field, value, error);
}

} // namespace

bool load_runtime_config(const std::string& directory,
                         RuntimeConfig& config,
                         std::string& error)
{
    Json::Value root;
    if (!read_json(path_join(directory, "vi.json"), root, error)) {
        return false;
    }
    const Json::Value& sensor = root["sensor1"];
    if (!sensor.isObject()) {
        error = "vi.json: missing sensor1";
        return false;
    }
    if (!required_string(sensor, "vi.json:sensor1", "name",
                         config.video_input.sensor_name, error) ||
        !required_int(sensor, "vi.json:sensor1", "lane_mode",
                      config.video_input.lane_mode, error) ||
        !required_int(sensor, "vi.json:sensor1", "max_w",
                      config.video_input.max_width, error) ||
        !required_int(sensor, "vi.json:sensor1", "max_h",
                      config.video_input.max_height, error) ||
        !required_int(sensor, "vi.json:sensor1", "vi_fr",
                      config.video_input.frame_rate, error) ||
        !required_int(sensor, "vi.json:sensor1", "wdr_mode",
                      config.video_input.wdr_mode, error)) {
        return false;
    }

    root.clear();
    if (!read_json(path_join(directory, "venc.json"), root, error)) {
        return false;
    }
    const Json::Value& encoder = root["venc1"];
    if (!encoder.isObject()) {
        error = "venc.json: missing venc1";
        return false;
    }
    if (!required_string(encoder, "venc.json:venc1", "name",
                         config.video_encoder.mode, error) ||
        !required_int(encoder, "venc.json:venc1", "w",
                      config.video_encoder.width, error) ||
        !required_int(encoder, "venc.json:venc1", "h",
                      config.video_encoder.height, error) ||
        !required_int(encoder, "venc.json:venc1", "fr",
                      config.video_encoder.frame_rate, error) ||
        !required_int(encoder, "venc.json:venc1", "bitrate",
                      config.video_encoder.bitrate_kbps, error) ||
        !required_int(encoder, "venc.json:venc1", "svc_enable",
                      config.video_encoder.svc_enabled, error)) {
        return false;
    }

    root.clear();
    if (!read_json(path_join(directory, "aenc.json"), root, error)) {
        return false;
    }
    const Json::Value& audio = root["aenc1"];
    if (!audio.isObject()) {
        error = "aenc.json: missing aenc1";
        return false;
    }
    int audio_enabled = 0;
    int microphone_enabled = 0;
    if (!required_int(audio, "aenc.json:aenc1", "enable", audio_enabled, error) ||
        !required_int(audio, "aenc.json:aenc1", "is_mic", microphone_enabled, error) ||
        !required_string(audio, "aenc.json:aenc1", "name",
                         config.audio_encoder.codec, error)) {
        return false;
    }
    config.audio_encoder.enabled = audio_enabled != 0;
    config.audio_encoder.microphone = microphone_enabled != 0;

    root.clear();
    if (!read_json(path_join(directory, "net_service.json"), root, error)) {
        return false;
    }
    const Json::Value& services = root["net_service"];
    const Json::Value& webrtc = services["webrtc"];
    if (!services.isObject() || !webrtc.isObject()) {
        error = "net_service.json: missing webrtc";
        return false;
    }
    int webrtc_enabled = 1;
    int detections_enabled = 1;
    int disable_twcc = 1;
    if (!optional_int(webrtc, "net_service.json:webrtc", "enable", 1,
                      webrtc_enabled, error) ||
        !required_string(webrtc, "net_service.json:webrtc", "signaling_url",
                         config.webrtc.signaling_url, error) ||
        !optional_int(webrtc, "net_service.json:webrtc", "detections_enable", 1,
                      detections_enabled, error) ||
        !optional_int(webrtc, "net_service.json:webrtc", "disable_twcc", 1,
                      disable_twcc, error) ||
        !optional_int(webrtc, "net_service.json:webrtc", "preview_stream_id", 1,
                      config.webrtc.preview_stream_id, error) ||
        !optional_int(webrtc, "net_service.json:webrtc", "rolling_buffer_sec", 1,
                      config.webrtc.rolling_buffer_sec, error) ||
        !optional_int(webrtc, "net_service.json:webrtc", "expected_bitrate_kbps", 1000,
                      config.webrtc.expected_bitrate_kbps, error) ||
        !optional_int(webrtc, "net_service.json:webrtc", "max_viewers", 3,
                      config.webrtc.max_viewers, error)) {
        return false;
    }
    config.webrtc.enabled = webrtc_enabled != 0;
    config.webrtc.detections_enabled = detections_enabled != 0;
    config.webrtc.disable_twcc = disable_twcc != 0;
    if (webrtc.isMember("signaling_token") &&
        !webrtc["signaling_token"].isString()) {
        error = "net_service.json:webrtc:signaling_token must be a string";
        return false;
    }
    config.webrtc.signaling_token =
        webrtc.get("signaling_token", "").asString();
    if (webrtc.isMember("room") && !webrtc["room"].isString()) {
        error = "net_service.json:webrtc:room must be a string";
        return false;
    }
    config.webrtc.room = webrtc.get("room", "door-1").asString();
    config.webrtc.ice_servers.clear();
    const Json::Value& ice = webrtc["ice_servers"];
    if (webrtc.isMember("ice_servers") && !ice.isArray()) {
        error = "net_service.json:webrtc:ice_servers must be an array";
        return false;
    }
    if (ice.isArray()) {
        for (const auto& item : ice) {
            if (!item.isObject() || !item.isMember("urls") ||
                !item["urls"].isString()) {
                error = "net_service.json:webrtc:ice_servers[].urls must be a string";
                return false;
            }
            WebRtcIceServerConfig server;
            server.urls = item["urls"].asString();
            if (item.isMember("username") && !item["username"].isString()) {
                error = "net_service.json:webrtc:ice_servers[].username must be a string";
                return false;
            }
            if (item.isMember("credential") && !item["credential"].isString()) {
                error = "net_service.json:webrtc:ice_servers[].credential must be a string";
                return false;
            }
            server.username = item.get("username", "").asString();
            server.credential = item.get("credential", "").asString();
            if (!server.urls.empty() && server.urls.size() <= 1024) {
                config.webrtc.ice_servers.push_back(std::move(server));
            }
        }
    }

    root.clear();
    if (!read_json(path_join(directory, "osd.json"), root, error)) {
        return false;
    }
    const Json::Value& time_osd = root["time_osd"];
    if (!time_osd.isObject()) {
        error = "osd.json: missing time_osd";
        return false;
    }
    int osd_enabled = 1;
    if (!required_int(time_osd, "osd.json:time_osd", "enable", osd_enabled, error) ||
        !required_int(time_osd, "osd.json:time_osd", "x", config.time_osd.x, error) ||
        !required_int(time_osd, "osd.json:time_osd", "y", config.time_osd.y, error) ||
        !required_int(time_osd, "osd.json:time_osd", "font_size",
                      config.time_osd.font_size, error)) {
        return false;
    }
    config.time_osd.enabled = osd_enabled != 0;

    root.clear();
    const std::string yolov8_config =
        path_join(directory, "../yolov8/yolov8.json");
    if (!read_json(yolov8_config, root, error)) {
        return false;
    }
    const Json::Value& yolov8 = root["yolov8"];
    if (!yolov8.isObject()) {
        error = "yolov8.json: missing yolov8";
        return false;
    }
    int yolov8_enabled = 1;
    if (!required_int(yolov8, "yolov8.json:yolov8", "enable", yolov8_enabled, error) ||
        !required_string(yolov8, "yolov8.json:yolov8", "cfg_file",
                         config.yolov8.config_file, error) ||
        !required_string(yolov8, "yolov8.json:yolov8", "model_file",
                         config.yolov8.model_file, error)) {
        return false;
    }
    config.yolov8.enabled = yolov8_enabled != 0;

    const bool supported_encoder_mode =
        config.video_encoder.mode == "H264_CBR" ||
        config.video_encoder.mode == "H264_AVBR" ||
        config.video_encoder.mode == "H265_CBR" ||
        config.video_encoder.mode == "H265_AVBR";
    const bool supported_signaling_scheme =
        config.webrtc.signaling_url.rfind("ws://", 0) == 0 ||
        config.webrtc.signaling_url.rfind("wss://", 0) == 0;
    if (config.video_input.sensor_name.empty() ||
        !supported_encoder_mode ||
        !supported_signaling_scheme ||
        !positive(config.video_input.max_width) ||
        !positive(config.video_input.max_height) ||
        !positive(config.video_input.frame_rate) ||
        !positive(config.video_encoder.width) ||
        !positive(config.video_encoder.height) ||
        !positive(config.video_encoder.frame_rate) ||
        !positive(config.video_encoder.bitrate_kbps) ||
        config.webrtc.signaling_url.empty() ||
        config.webrtc.signaling_url.size() > 2048 ||
        config.webrtc.room.empty() || config.webrtc.room.size() > 128 ||
        config.webrtc.preview_stream_id < 0 ||
        config.webrtc.preview_stream_id > 2 ||
        config.webrtc.rolling_buffer_sec < 0 ||
        config.webrtc.rolling_buffer_sec > 30 ||
        !positive(config.webrtc.expected_bitrate_kbps) ||
        config.webrtc.max_viewers < 1 ||
        config.webrtc.max_viewers > 3 ||
        config.time_osd.x < 0 || config.time_osd.y < 0 ||
        !positive(config.time_osd.font_size)) {
        error = "configuration contains unsupported mode, URL, stream, or numeric value";
        return false;
    }
    if (config.yolov8.enabled &&
        (config.yolov8.config_file.empty() ||
         config.yolov8.model_file.empty())) {
        error = "YOLOv8 is enabled but cfg_file or model_file is empty";
        return false;
    }
    if (config.audio_encoder.enabled &&
        config.audio_encoder.codec != "AAC" &&
        config.audio_encoder.codec != "G711U") {
        error = "aenc.json: name must be AAC or G711U";
        return false;
    }
    return true;
}

} // namespace zero_ipc::config
