#include "config/runtime_config.h"

#include <json/json.h>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <utility>

namespace ipc_mini::config {
namespace {

constexpr long kMaxConfigBytes = 64 * 1024;

bool file_is_regular(const std::string& path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string path_join(const std::string& directory, const char* file)
{
    if (!directory.empty() && directory.back() == '/') {
        return directory + file;
    }
    return directory + "/" + file;
}

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

bool required_string(const Json::Value& object, const char* where,
                     const char* field, std::string& value,
                     std::string& error)
{
    if (!object.isObject() || !object.isMember(field) ||
        !object[field].isString() || object[field].asString().empty()) {
        error = std::string(where) + "." + field +
                " must be a non-empty string";
        return false;
    }
    value = object[field].asString();
    return true;
}

bool optional_string(const Json::Value& object, const char* where,
                     const char* field, const std::string& default_value,
                     std::string& value, std::string& error)
{
    if (!object.isObject() || !object.isMember(field)) {
        value = default_value;
        return true;
    }
    if (!object[field].isString()) {
        error = std::string(where) + "." + field + " must be a string";
        return false;
    }
    value = object[field].asString();
    return true;
}

bool required_int(const Json::Value& object, const char* where,
                  const char* field, int& value, std::string& error)
{
    if (!object.isObject() || !object.isMember(field) ||
        !object[field].isInt()) {
        error = std::string(where) + "." + field + " must be an integer";
        return false;
    }
    value = object[field].asInt();
    return true;
}

bool optional_int(const Json::Value& object, const char* where,
                  const char* field, int default_value, int& value,
                  std::string& error)
{
    if (!object.isObject() || !object.isMember(field)) {
        value = default_value;
        return true;
    }
    return required_int(object, where, field, value, error);
}

bool parse_bool(const Json::Value& value, bool& out)
{
    if (value.isBool()) {
        out = value.asBool();
        return true;
    }
    if (value.isInt()) {
        out = value.asInt() != 0;
        return true;
    }
    return false;
}

bool required_bool(const Json::Value& object, const char* where,
                   const char* field, bool& value, std::string& error)
{
    if (!object.isObject() || !object.isMember(field) ||
        !parse_bool(object[field], value)) {
        error = std::string(where) + "." + field +
                " must be a boolean or 0/1";
        return false;
    }
    return true;
}

bool optional_bool(const Json::Value& object, const char* where,
                   const char* field, bool default_value, bool& value,
                   std::string& error)
{
    if (!object.isObject() || !object.isMember(field)) {
        value = default_value;
        return true;
    }
    return required_bool(object, where, field, value, error);
}

bool parse_start(const Json::Value& object, const char* where,
                 StreamStart default_value, StreamStart& value,
                 std::string& error)
{
    if (!object.isObject() || !object.isMember("start")) {
        value = default_value;
        return true;
    }
    if (!object["start"].isString()) {
        error = std::string(where) + ".start must be \"boot\" or \"on_demand\"";
        return false;
    }
    const std::string text = object["start"].asString();
    if (text == "boot") {
        value = StreamStart::Boot;
        return true;
    }
    if (text == "on_demand") {
        value = StreamStart::OnDemand;
        return true;
    }
    error = std::string(where) + ".start must be \"boot\" or \"on_demand\"";
    return false;
}

bool supported_encoder_mode(const std::string& mode)
{
    return mode == "H264_CBR" || mode == "H264_AVBR" ||
        mode == "H265_CBR" || mode == "H265_AVBR";
}

bool positive(int value)
{
    return value > 0;
}

bool parse_osd(const Json::Value& object, const char* where,
               TimeOsdConfig& osd, std::string& error);

bool parse_encoded_stream(const Json::Value& object, const char* where,
                          const EncodedStreamConfig& defaults,
                          EncodedStreamConfig& stream, std::string& error)
{
    if (!object.isObject()) {
        error = std::string(where) + " must be an object";
        return false;
    }
    if (!optional_bool(object, where, "enable", defaults.enable,
                       stream.enable, error) ||
        !parse_start(object, where, defaults.start, stream.start, error)) {
        return false;
    }
    const Json::Value& encoder =
        object.isMember("encoder") ? object["encoder"] : object;
    const char* encoder_where =
        object.isMember("encoder") ? "encoder" : where;
    std::string encoder_where_buf;
    if (object.isMember("encoder")) {
        encoder_where_buf = std::string(where) + ".encoder";
        encoder_where = encoder_where_buf.c_str();
    }
    if (!optional_string(encoder, encoder_where, "mode", defaults.mode,
                         stream.mode, error) ||
        !optional_int(encoder, encoder_where, "width", defaults.width,
                      stream.width, error) ||
        !optional_int(encoder, encoder_where, "height", defaults.height,
                      stream.height, error) ||
        !optional_int(encoder, encoder_where, "fps", defaults.fps,
                      stream.fps, error) ||
        !optional_int(encoder, encoder_where, "bitrate_kbps",
                      defaults.bitrate_kbps, stream.bitrate_kbps, error) ||
        !optional_bool(encoder, encoder_where, "svc", defaults.svc,
                       stream.svc, error)) {
        return false;
    }
    stream.osd = defaults.osd;
    if (object.isMember("osd")) {
        const std::string osd_where = std::string(where) + ".osd";
        if (!parse_osd(object["osd"], osd_where.c_str(), stream.osd, error)) {
            return false;
        }
    }
    return true;
}

bool parse_osd(const Json::Value& object, const char* where,
               TimeOsdConfig& osd, std::string& error)
{
    if (!object.isObject()) {
        error = std::string(where) + " must be an object";
        return false;
    }
    return optional_bool(object, where, "enable", true, osd.enable, error) &&
        optional_int(object, where, "x", 32, osd.x, error) &&
        optional_int(object, where, "y", 32, osd.y, error) &&
        optional_int(object, where, "font_size", 32, osd.font_size, error);
}

bool parse_ai_stream(const Json::Value& object, const char* where,
                     AiStreamConfig& ai, std::string& error)
{
    if (!object.isObject()) {
        error = std::string(where) + " must be an object";
        return false;
    }
    if (!optional_bool(object, where, "enable", false, ai.enable, error) ||
        !parse_start(object, where, StreamStart::OnDemand, ai.start, error) ||
        !optional_string(object, where, "engine", "yolov8", ai.engine,
                         error) ||
        !optional_string(object, where, "model", "", ai.model, error) ||
        !optional_string(object, where, "acl", "", ai.acl, error) ||
        !optional_int(object, where, "width", 640, ai.width, error) ||
        !optional_int(object, where, "height", 640, ai.height, error) ||
        !optional_int(object, where, "fps", 1, ai.fps, error) ||
        !optional_int(object, where, "idle_stop_sec", 5, ai.idle_stop_sec,
                      error)) {
        return false;
    }
    return true;
}

bool parse_ice_servers(const Json::Value& ice, const char* where,
                       std::vector<WebRtcIceServerConfig>& servers,
                       std::string& error)
{
    servers.clear();
    if (ice.isNull()) {
        return true;
    }
    if (!ice.isArray()) {
        error = std::string(where) + " must be an array";
        return false;
    }
    for (const auto& item : ice) {
        if (!item.isObject() || !item.isMember("urls") ||
            !item["urls"].isString()) {
            error = std::string(where) + "[].urls must be a string";
            return false;
        }
        WebRtcIceServerConfig server;
        server.urls = item["urls"].asString();
        if (item.isMember("username") && !item["username"].isString()) {
            error = std::string(where) + "[].username must be a string";
            return false;
        }
        if (item.isMember("credential") && !item["credential"].isString()) {
            error = std::string(where) + "[].credential must be a string";
            return false;
        }
        server.username = item.get("username", "").asString();
        server.credential = item.get("credential", "").asString();
        if (!server.urls.empty() && server.urls.size() <= 1024) {
            servers.push_back(std::move(server));
        }
    }
    return true;
}

bool parse_webrtc(const Json::Value& object, const char* where,
                  WebRtcConfig& webrtc, std::string& error)
{
    if (!object.isObject()) {
        error = std::string(where) + " must be an object";
        return false;
    }
    if (!optional_bool(object, where, "enable", true, webrtc.enable, error) ||
        !required_string(object, where, "signaling_url", webrtc.signaling_url,
                         error) ||
        !optional_string(object, where, "signaling_token", "",
                         webrtc.signaling_token, error) ||
        !optional_string(object, where, "room", "door-1", webrtc.room,
                         error) ||
        !optional_string(object, where, "preview", "sub", webrtc.preview,
                         error) ||
        !optional_bool(object, where, "send_detections", true,
                       webrtc.send_detections, error) ||
        !optional_bool(object, where, "disable_twcc", true,
                       webrtc.disable_twcc, error) ||
        !optional_int(object, where, "rolling_buffer_sec", 1,
                      webrtc.rolling_buffer_sec, error) ||
        !optional_int(object, where, "expected_bitrate_kbps", 1000,
                      webrtc.expected_bitrate_kbps, error) ||
        !optional_int(object, where, "max_viewers", 3, webrtc.max_viewers,
                      error) ||
        !parse_ice_servers(object["ice_servers"],
                           (std::string(where) + ".ice_servers").c_str(),
                           webrtc.ice_servers, error)) {
        return false;
    }
    if (object.isMember("detections_enable") &&
        !object.isMember("send_detections") &&
        !optional_bool(object, where, "detections_enable", true,
                       webrtc.send_detections, error)) {
        return false;
    }
    if (object.isMember("preview_stream_id") &&
        webrtc.preview == "sub") {
        int preview_id = 1;
        if (!optional_int(object, where, "preview_stream_id", 1, preview_id,
                          error)) {
            return false;
        }
        if (preview_id == 0) {
            webrtc.preview = "main";
        } else if (preview_id == 2) {
            webrtc.preview = "ai";
        } else {
            webrtc.preview = "sub";
        }
    }
    webrtc.preview_stream_id = stream_id_from_name(webrtc.preview);
    if (webrtc.preview_stream_id < 0) {
        error = std::string(where) + ".preview must be main, sub, or ai";
        return false;
    }
    return true;
}

bool validate_config(const RuntimeConfig& config, std::string& error)
{
    const bool supported_signaling =
        config.webrtc.signaling_url.rfind("ws://", 0) == 0 ||
        config.webrtc.signaling_url.rfind("wss://", 0) == 0;
    const auto& main = config.streams.main;
    const auto& sub = config.streams.sub;
    const auto& ai = config.streams.ai;
    if (config.sensor.name.empty() ||
        !positive(config.sensor.width) ||
        !positive(config.sensor.height) ||
        !positive(config.sensor.fps) ||
        !main.enable ||
        !supported_encoder_mode(main.mode) ||
        !positive(main.width) || !positive(main.height) ||
        !positive(main.fps) || !positive(main.bitrate_kbps) ||
        (sub.enable &&
         (!supported_encoder_mode(sub.mode) ||
          !positive(sub.width) || !positive(sub.height) ||
          !positive(sub.fps) || !positive(sub.bitrate_kbps))) ||
        (ai.enable &&
         (ai.engine != "yolov8" || ai.model.empty() || ai.acl.empty() ||
          !positive(ai.width) || !positive(ai.height) ||
          !positive(ai.fps) || ai.idle_stop_sec < 1 ||
          ai.idle_stop_sec > 60)) ||
        !supported_signaling ||
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
        (main.osd.enable &&
         (main.osd.x < 0 || main.osd.y < 0 ||
          !positive(main.osd.font_size))) ||
        (sub.osd.enable &&
         (sub.osd.x < 0 || sub.osd.y < 0 ||
          !positive(sub.osd.font_size))) ||
        (config.record.enable &&
         (config.record.directory.empty() ||
          config.record.stream_id < 0 || config.record.stream_id > 1 ||
          config.record.segment_sec < 5 ||
          config.record.segment_sec > 3600 ||
          (!config.record.upload_url.empty() &&
           config.record.upload_url.rfind("http://", 0) != 0)))) {
        error = "configuration contains unsupported mode, URL, stream, or numeric value";
        return false;
    }
    if (config.audio.enable &&
        config.audio.codec != "AAC" &&
        config.audio.codec != "G711U") {
        error = "audio.codec must be AAC or G711U";
        return false;
    }
    return true;
}

void apply_inherit_modes(StreamsConfig& streams)
{
    if (streams.sub.mode == "inherit" || streams.sub.mode.empty()) {
        streams.sub.mode = streams.main.mode;
    }
}

bool load_ipc_mini_json(const std::string& path, RuntimeConfig& config,
                        std::string& error)
{
    Json::Value root;
    if (!read_json(path, root, error)) {
        return false;
    }
    if (!root.isObject()) {
        error = "ipc_mini.json: root must be an object";
        return false;
    }

    const Json::Value& sensor = root["sensor"];
    if (!sensor.isObject()) {
        error = "ipc_mini.json: missing sensor";
        return false;
    }
    if (!required_string(sensor, "sensor", "name", config.sensor.name, error) ||
        !optional_int(sensor, "sensor", "lane_mode", 0,
                      config.sensor.lane_mode, error) ||
        !required_int(sensor, "sensor", "width", config.sensor.width, error) ||
        !required_int(sensor, "sensor", "height", config.sensor.height,
                      error) ||
        !required_int(sensor, "sensor", "fps", config.sensor.fps, error) ||
        !optional_int(sensor, "sensor", "wdr", 0, config.sensor.wdr, error)) {
        return false;
    }

    if (root.isMember("audio")) {
        const Json::Value& audio = root["audio"];
        if (!audio.isObject()) {
            error = "audio must be an object";
            return false;
        }
        if (!optional_bool(audio, "audio", "enable", false,
                           config.audio.enable, error) ||
            !optional_bool(audio, "audio", "microphone", false,
                           config.audio.microphone, error) ||
            !optional_string(audio, "audio", "codec", "G711U",
                             config.audio.codec, error)) {
            return false;
        }
    }

    const Json::Value& streams = root["streams"];
    if (!streams.isObject()) {
        error = "ipc_mini.json: missing streams";
        return false;
    }
    EncodedStreamConfig main_defaults;
    EncodedStreamConfig sub_defaults;
    sub_defaults.start = StreamStart::OnDemand;
    sub_defaults.mode = "inherit";
    sub_defaults.width = 720;
    sub_defaults.height = 480;
    sub_defaults.bitrate_kbps = 1000;
    if (!parse_encoded_stream(streams["main"], "streams.main", main_defaults,
                              config.streams.main, error) ||
        !parse_encoded_stream(streams["sub"], "streams.sub", sub_defaults,
                              config.streams.sub, error) ||
        !parse_ai_stream(streams["ai"], "streams.ai", config.streams.ai,
                         error)) {
        return false;
    }
    if (!streams["sub"].isObject() || !streams["sub"].isMember("osd")) {
        if (root.isMember("osd") &&
            !parse_osd(root["osd"], "osd", config.streams.sub.osd, error)) {
            return false;
        }
    }
    apply_inherit_modes(config.streams);

    if (root.isMember("record")) {
        const Json::Value& record = root["record"];
        if (!record.isObject()) {
            error = "record must be an object";
            return false;
        }
        if (!optional_bool(record, "record", "enable", false,
                           config.record.enable, error) ||
            !optional_string(record, "record", "stream", "main",
                             config.record.stream, error) ||
            !optional_bool(record, "record", "audio", true,
                           config.record.audio, error) ||
            !optional_int(record, "record", "segment_sec", 300,
                          config.record.segment_sec, error) ||
            !optional_string(record, "record", "directory", "/mnt/record",
                             config.record.directory, error) ||
            !optional_string(record, "record", "upload_url", "",
                             config.record.upload_url, error) ||
            !optional_string(record, "record", "upload_token", "",
                             config.record.upload_token, error)) {
            return false;
        }
        if (record.isMember("file") && !record.isMember("directory") &&
            record["file"].isString()) {
            const std::string file = record["file"].asString();
            const auto slash = file.find_last_of('/');
            if (slash != std::string::npos && slash > 0) {
                config.record.directory = file.substr(0, slash);
            }
        }
        if (record.isMember("dir") && record["dir"].isString() &&
            !record["dir"].asString().empty()) {
            config.record.directory = record["dir"].asString();
        }
        config.record.stream_id = stream_id_from_name(config.record.stream);
        if (config.record.enable && config.record.stream_id < 0) {
            error = "record.stream must be main or sub";
            return false;
        }
    }

    if (!root.isMember("webrtc")) {
        error = "ipc_mini.json: missing webrtc";
        return false;
    }
    if (!parse_webrtc(root["webrtc"], "webrtc", config.webrtc, error)) {
        return false;
    }
    return validate_config(config, error);
}

bool load_legacy_files(const std::string& directory, RuntimeConfig& config,
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
                         config.sensor.name, error) ||
        !required_int(sensor, "vi.json:sensor1", "lane_mode",
                      config.sensor.lane_mode, error) ||
        !required_int(sensor, "vi.json:sensor1", "max_w",
                      config.sensor.width, error) ||
        !required_int(sensor, "vi.json:sensor1", "max_h",
                      config.sensor.height, error) ||
        !required_int(sensor, "vi.json:sensor1", "vi_fr",
                      config.sensor.fps, error) ||
        !required_int(sensor, "vi.json:sensor1", "wdr_mode",
                      config.sensor.wdr, error)) {
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
    int svc = 0;
    if (!required_string(encoder, "venc.json:venc1", "name",
                         config.streams.main.mode, error) ||
        !required_int(encoder, "venc.json:venc1", "w",
                      config.streams.main.width, error) ||
        !required_int(encoder, "venc.json:venc1", "h",
                      config.streams.main.height, error) ||
        !required_int(encoder, "venc.json:venc1", "fr",
                      config.streams.main.fps, error) ||
        !required_int(encoder, "venc.json:venc1", "bitrate",
                      config.streams.main.bitrate_kbps, error) ||
        !required_int(encoder, "venc.json:venc1", "svc_enable", svc, error)) {
        return false;
    }
    config.streams.main.enable = true;
    config.streams.main.start = StreamStart::Boot;
    config.streams.main.svc = svc != 0;
    config.streams.sub.enable = true;
    config.streams.sub.start = StreamStart::OnDemand;
    config.streams.sub.mode = config.streams.main.mode;
    config.streams.sub.width = 720;
    config.streams.sub.height = 480;
    config.streams.sub.fps = config.streams.main.fps;
    config.streams.sub.bitrate_kbps = 1000;

    root.clear();
    if (!read_json(path_join(directory, "aenc.json"), root, error)) {
        return false;
    }
    const Json::Value& audio = root["aenc1"];
    if (!audio.isObject()) {
        error = "aenc.json: missing aenc1";
        return false;
    }
    if (!required_bool(audio, "aenc.json:aenc1", "enable",
                       config.audio.enable, error) ||
        !required_bool(audio, "aenc.json:aenc1", "is_mic",
                       config.audio.microphone, error) ||
        !required_string(audio, "aenc.json:aenc1", "name",
                         config.audio.codec, error)) {
        return false;
    }

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
    if (!parse_webrtc(webrtc, "net_service.json:webrtc", config.webrtc,
                      error)) {
        return false;
    }

    root.clear();
    if (!read_json(path_join(directory, "osd.json"), root, error)) {
        return false;
    }
    if (!parse_osd(root["time_osd"], "osd.json:time_osd",
                   config.streams.sub.osd, error)) {
        return false;
    }

    root.clear();
    if (!read_json(path_join(directory, "../yolov8/yolov8.json"), root,
                   error)) {
        return false;
    }
    const Json::Value& yolov8 = root["yolov8"];
    if (!yolov8.isObject()) {
        error = "yolov8.json: missing yolov8";
        return false;
    }
    if (!required_bool(yolov8, "yolov8.json:yolov8", "enable",
                       config.streams.ai.enable, error) ||
        !required_string(yolov8, "yolov8.json:yolov8", "cfg_file",
                         config.streams.ai.acl, error) ||
        !required_string(yolov8, "yolov8.json:yolov8", "model_file",
                         config.streams.ai.model, error)) {
        return false;
    }
    config.streams.ai.start = StreamStart::OnDemand;
    config.streams.ai.engine = "yolov8";
    config.streams.ai.width = 640;
    config.streams.ai.height = 640;
    config.streams.ai.fps = 1;

    const std::string record_path = path_join(directory, "mp4_save_info.json");
    if (file_is_regular(record_path)) {
        root.clear();
        if (!read_json(record_path, root, error)) {
            return false;
        }
        const Json::Value& record = root["mp4_save"];
        if (!record.isObject()) {
            error = "mp4_save_info.json: missing mp4_save";
            return false;
        }
        if (!optional_bool(record, "mp4_save_info.json:mp4_save", "enable",
                           false, config.record.enable, error) ||
            !optional_string(record, "mp4_save_info.json:mp4_save", "stream",
                             "main", config.record.stream, error) ||
            !optional_bool(record, "mp4_save_info.json:mp4_save", "audio",
                           true, config.record.audio, error) ||
            !optional_int(record, "mp4_save_info.json:mp4_save", "segment_sec",
                          300, config.record.segment_sec, error) ||
            !optional_string(record, "mp4_save_info.json:mp4_save", "dir",
                             "/mnt/record", config.record.directory, error) ||
            !optional_string(record, "mp4_save_info.json:mp4_save",
                             "upload_url", "", config.record.upload_url,
                             error)) {
            return false;
        }
        if (record.isMember("file") && record["file"].isString() &&
            !record.isMember("dir")) {
            const std::string file = record["file"].asString();
            const auto slash = file.find_last_of('/');
            if (slash != std::string::npos && slash > 0) {
                config.record.directory = file.substr(0, slash);
            }
        }
        config.record.stream_id = stream_id_from_name(config.record.stream);
    }

    return validate_config(config, error);
}

} // namespace

bool load_runtime_config(const std::string& directory,
                         RuntimeConfig& config,
                         std::string& error)
{
    const std::string unified = path_join(directory, "ipc_mini.json");
    if (file_is_regular(unified)) {
        return load_ipc_mini_json(unified, config, error);
    }
    return load_legacy_files(directory, config, error);
}

void resolve_runtime_paths(RuntimeConfig& config,
                           const std::string& install_root)
{
    auto resolve = [&install_root](std::string path) {
        static constexpr char kLegacyZeroMini[] = "/opt/zero_mini";
        static constexpr char kLegacyIpcMini[] = "/opt/ipc_mini";
        if (path.empty()) {
            return path;
        }
        if (path.rfind(kLegacyIpcMini, 0) == 0) {
            return install_root + path.substr(sizeof(kLegacyIpcMini) - 1);
        }
        if (path.rfind(kLegacyZeroMini, 0) == 0) {
            return install_root + path.substr(sizeof(kLegacyZeroMini) - 1);
        }
        if (path[0] == '/') {
            return path;
        }
        if (!install_root.empty() && install_root.back() == '/') {
            return install_root + path;
        }
        return install_root + "/" + path;
    };
    config.streams.ai.model = resolve(config.streams.ai.model);
    config.streams.ai.acl = resolve(config.streams.ai.acl);
}

std::string format_streams_summary(const RuntimeConfig& config)
{
    const auto& main = config.streams.main;
    const auto& sub = config.streams.sub;
    const auto& ai = config.streams.ai;
    char buffer[384];
    std::snprintf(
        buffer, sizeof(buffer),
        "main %dx%d@%d %s%s | sub %dx%d@%d %s%s | ai %dx%d@%d %s %s | record %s",
        main.width, main.height, main.fps, stream_start_name(main.start),
        main.osd.enable ? " osd" : "",
        sub.width, sub.height, sub.fps, stream_start_name(sub.start),
        sub.osd.enable ? " osd" : "",
        ai.width, ai.height, ai.fps, stream_start_name(ai.start),
        ai.enable ? ai.engine.c_str() : "off",
        config.record.enable ? "on" : "off");
    return buffer;
}

} // namespace ipc_mini::config
