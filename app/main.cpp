/**
 * zero-mini composition root: HisiliconPipeline + WebRtcPlugin only.
 */

#include "core/application.h"
#include "core/resource_profile.h"
#include "config/runtime_config.h"
#include "hisilicon_pipeline.h"
#include "protocol/webrtc_plugin.h"

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <ucontext.h>
#include <limits.h>
#include <sys/stat.h>

namespace {

std::atomic<bool> g_shutdown_requested{false};
std::mutex g_shutdown_mutex;
std::condition_variable g_shutdown_cv;

void request_shutdown(int)
{
    g_shutdown_requested.store(true);
    g_shutdown_cv.notify_all();
}

void write_hex(char*& out, uintptr_t value)
{
    static constexpr char digits[] = "0123456789abcdef";
    *out++ = '0';
    *out++ = 'x';
    for (int shift = static_cast<int>(sizeof(value) * 8) - 4; shift >= 0; shift -= 4) {
        *out++ = digits[(value >> shift) & 0xf];
    }
}

void on_fatal_signal(int sig, siginfo_t* info, void* context)
{
    uintptr_t pc = 0;
#if defined(__arm__)
    const auto* uc = static_cast<const ucontext_t*>(context);
    pc = static_cast<uintptr_t>(uc->uc_mcontext.arm_pc);
#endif
    char buf[128];
    char* p = buf;
    const char prefix[] = "\n[zero_mini] FATAL sig=";
    std::memcpy(p, prefix, sizeof(prefix) - 1);
    p += sizeof(prefix) - 1;
    *p++ = static_cast<char>('0' + (sig / 10));
    *p++ = static_cast<char>('0' + (sig % 10));
    const char at[] = " pc=";
    std::memcpy(p, at, sizeof(at) - 1);
    p += sizeof(at) - 1;
    write_hex(p, pc);
    *p++ = '\n';
    (void)!write(STDERR_FILENO, buf, static_cast<size_t>(p - buf));
    _exit(128 + sig);
}

void install_fatal_handler(int sig)
{
    struct sigaction action {};
    action.sa_sigaction = on_fatal_signal;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    sigaction(sig, &action, nullptr);
}

struct CommandLineOptions {
    zero_ipc::device_adapter::HisiliconPipelineOptions device{};
    std::string install_root{"/opt/zero_mini"};
    std::string config_directory{"/opt/zero_mini/etc"};
    zero_ipc::protocol::WebRtcPluginOptions webrtc{};
};

bool path_is_dir(const std::string& path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool path_is_file(const std::string& path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string dirname_of(const std::string& path)
{
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

std::string resolve_exe_dir(const char* argv0)
{
    char exe[PATH_MAX]{};
    const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        return dirname_of(exe);
    }
    if (argv0 && argv0[0] != '\0') {
        if (argv0[0] == '/') {
            return dirname_of(argv0);
        }
        char cwd[PATH_MAX]{};
        if (::getcwd(cwd, sizeof(cwd))) {
            return dirname_of(std::string(cwd) + "/" + argv0);
        }
    }
    return "/opt/zero_mini";
}

std::string install_root_from_config_dir(const std::string& config_directory)
{
    return dirname_of(config_directory);
}

std::string remap_legacy_opt_path(const std::string& path,
                                  const std::string& install_root)
{
    static constexpr char kLegacy[] = "/opt/zero_mini";
    if (path.rfind(kLegacy, 0) != 0) {
        return path;
    }
    return install_root + path.substr(sizeof(kLegacy) - 1);
}

/**
 * Config dir: --config-dir if given; else <exe-dir>/etc; else /opt/zero_mini/etc.
 * Fonts/yolov8 resolve from that config dir's parent.
 */
std::string find_config_directory(int argc, char** argv, const char* argv0)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--config-dir") == 0) {
            return argv[i + 1];
        }
    }
    const std::string beside_exe = resolve_exe_dir(argv0) + "/etc";
    if (path_is_dir(beside_exe)) {
        return beside_exe;
    }
    return "/opt/zero_mini/etc";
}

void apply_runtime_config(const zero_ipc::config::RuntimeConfig& config,
                          CommandLineOptions& command_line)
{
    command_line.device.lane_mode =
        static_cast<lane_divide_mode_t>(config.video_input.lane_mode);
    command_line.device.sensor_width = config.video_input.max_width;
    command_line.device.sensor_height = config.video_input.max_height;
    command_line.device.sensor_fps = config.video_input.frame_rate;
    command_line.device.wdr_mode = config.video_input.wdr_mode;
    command_line.device.sensor_name = config.video_input.sensor_name;
    command_line.device.encoder_mode = config.video_encoder.mode;
    command_line.device.video_width = config.video_encoder.width;
    command_line.device.video_height = config.video_encoder.height;
    command_line.device.video_fps = config.video_encoder.frame_rate;
    command_line.device.bitrate_kbps = config.video_encoder.bitrate_kbps;
    command_line.device.svc_enabled = config.video_encoder.svc_enabled;
    command_line.device.audio_enabled = config.audio_encoder.enabled;
    command_line.device.audio_microphone = config.audio_encoder.microphone;
    command_line.device.audio_codec = config.audio_encoder.codec;
    command_line.device.time_osd_enabled = config.time_osd.enabled;
    command_line.device.time_osd_x = config.time_osd.x;
    command_line.device.time_osd_y = config.time_osd.y;
    command_line.device.time_osd_font_size = config.time_osd.font_size;
    command_line.device.yolov8_enabled = config.yolov8.enabled;
    command_line.device.yolov8_config_file = remap_legacy_opt_path(
        config.yolov8.config_file, command_line.install_root);
    command_line.device.yolov8_model_file = remap_legacy_opt_path(
        config.yolov8.model_file, command_line.install_root);

    command_line.webrtc.signaling_url = config.webrtc.signaling_url;
    command_line.webrtc.signaling_token = config.webrtc.signaling_token;
    command_line.webrtc.room = config.webrtc.room;
    command_line.webrtc.preview_stream_id = config.webrtc.preview_stream_id;
    command_line.webrtc.detections_enabled = config.webrtc.detections_enabled;
    command_line.webrtc.disable_twcc = config.webrtc.disable_twcc;
    command_line.webrtc.rolling_buffer_sec = config.webrtc.rolling_buffer_sec;
    command_line.webrtc.expected_bitrate_bps =
        config.webrtc.expected_bitrate_kbps * 1000;
    command_line.webrtc.max_viewers = config.webrtc.max_viewers;
    command_line.webrtc.ice_servers.clear();
    for (const auto& ice : config.webrtc.ice_servers) {
        zero_ipc::protocol::WebRtcIceServer s;
        s.urls = ice.urls;
        s.username = ice.username;
        s.credential = ice.credential;
        command_line.webrtc.ice_servers.push_back(std::move(s));
    }
}

} // namespace

int main(int argc, char** argv)
{
    using namespace zero_ipc;

    CommandLineOptions command_line;
    command_line.config_directory =
        find_config_directory(argc, argv, argv[0]);
    command_line.install_root =
        install_root_from_config_dir(command_line.config_directory);

    config::RuntimeConfig runtime_config;
    std::string config_error;
    if (!config::load_runtime_config(
            command_line.config_directory, runtime_config, config_error)) {
        std::fprintf(stderr, "[zero_mini] config error: %s\n",
                     config_error.c_str());
        std::fprintf(stderr,
                     "[zero_mini] hint: place etc/ beside binary, or pass "
                     "--config-dir <package>/etc\n");
        return 1;
    }
    apply_runtime_config(runtime_config, command_line);

    const std::string font_path =
        command_line.install_root + "/fonts/DejaVuSans.ttf";
    if (path_is_file(font_path)) {
        command_line.device.font_path = font_path;
    }
    std::printf("[zero_mini] config_dir=%s install_root=%s font=%s\n",
                command_line.config_directory.c_str(),
                command_line.install_root.c_str(),
                command_line.device.font_path.empty()
                    ? "(default)"
                    : command_line.device.font_path.c_str());

    {
        // Prefer sigaction so SIGINT interrupts blocking syscalls (no SA_RESTART).
        struct sigaction sa {};
        sa.sa_handler = request_shutdown;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
    }
    install_fatal_handler(SIGILL);
    install_fatal_handler(SIGSEGV);
    install_fatal_handler(SIGBUS);
    install_fatal_handler(SIGABRT);

    core::ApplicationOptions app_opts;
    app_opts.profile = core::ResourceProfile::hisi_32mb();
    app_opts.channel_id = command_line.device.channel_id;

    core::Application app(
        std::move(app_opts),
        std::make_unique<device_adapter::HisiliconPipeline>(command_line.device));

    if (runtime_config.webrtc.enabled) {
        app.add_protocol(std::make_unique<protocol::WebRtcPlugin>(
            command_line.webrtc));
    }

    std::printf(
        "[zero_mini] starting sensor=%s venc=%s %dx%d webrtc=%s room=%s\n",
        command_line.device.sensor_name.c_str(),
        command_line.device.encoder_mode.c_str(),
        command_line.device.video_width, command_line.device.video_height,
        runtime_config.webrtc.enabled ? "on" : "off",
        command_line.webrtc.room.c_str());
    std::printf("[zero_mini] signaling %s\n",
                command_line.webrtc.signaling_url.c_str());

    if (!app.start()) {
        std::fprintf(stderr, "[zero_mini] start failed\n");
        return 2;
    }
    std::printf("[zero_mini] start ok — waiting for viewer via signaling\n");
    std::fflush(stdout);

    {
        std::unique_lock lock(g_shutdown_mutex);
        g_shutdown_cv.wait(lock, [] {
            return g_shutdown_requested.load();
        });
    }

    std::printf("[zero_mini] stopping...\n");
    std::fflush(stdout);

    // KVS freePeerConnection / usrsctp can block forever on this platform.
    // Hard-exit if graceful teardown exceeds the budget so Ctrl+C always works.
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::fprintf(stderr,
                     "[zero_mini] stop watchdog fired — forcing exit\n");
        std::fflush(stderr);
        _exit(1);
    }).detach();

    app.stop();
    std::printf("[zero_mini] bye\n");
    std::fflush(stdout);
    return 0;
}
