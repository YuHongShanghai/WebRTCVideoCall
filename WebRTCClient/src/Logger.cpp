#include "Logger.h"

#include <filesystem>
#include <spdlog/sinks/daily_file_sink.h>

std::shared_ptr<spdlog::logger> Logger::s_logger = nullptr;

void Logger::init(std::string appName) {
    if (s_logger) return;

    std::string logDir = std::string(getenv("HOME")) + "/webrtcclient/logs";
    std::string logFileName = logDir + "/" + appName + ".log";
    if (!std::filesystem::exists(logDir)) {
        std::filesystem::create_directories(logDir);
    }
    auto fileSink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
        logFileName, 0, 0, /*truncate=*/false, /*max_files=*/7);
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};

    s_logger = std::make_shared<spdlog::logger>(appName, sinks.begin(), sinks.end());
    s_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%P %t] %^%v%$");
    s_logger->set_level(spdlog::level::debug); // 设置最低日志等级
    s_logger->flush_on(spdlog::level::warn); // 高级别立刻落盘
    spdlog::flush_every(std::chrono::seconds(10)); // 常规日志 10s 内落盘

    av_log_set_level(AV_LOG_WARNING);  // 控制 FFmpeg 自身输出的详细度
#if defined(AV_LOG_SKIP_REPEATED)
    av_log_set_flags(AV_LOG_SKIP_REPEATED | AV_LOG_PRINT_LEVEL);
#endif
    av_log_set_callback(ffmpeg_log_callback);
}
