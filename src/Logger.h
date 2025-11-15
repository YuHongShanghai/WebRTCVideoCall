#pragma once
#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
extern "C" {
#include <libavutil/log.h>
}

#define Logd(fmt, ...) Logger::logd(__FUNCTION__, fmt, ##__VA_ARGS__)
#define Logi(fmt, ...) Logger::logi(__FUNCTION__, fmt, ##__VA_ARGS__)
#define Logw(fmt, ...) Logger::logw(__FUNCTION__, fmt, ##__VA_ARGS__)
#define Loge(fmt, ...) Logger::loge(__FUNCTION__, fmt, ##__VA_ARGS__)
#define Logendl() Logger::logendl()

class Logger {
public:
    static void init(std::string appName); // 初始化 logger，设置格式、输出

    static void logd(const std::string& tag, const std::string& msg) {
        s_logger->log(spdlog::level::debug, "[{}] {}", tag, msg);
    }

    static void logi(const std::string& tag, const std::string& msg) {
        s_logger->log(spdlog::level::info, "[{}] {}", tag, msg);
    }

    static void logw(const std::string& tag, const std::string& msg) {
        s_logger->log(spdlog::level::warn, "[{}] {}", tag, msg);
    }

    static void loge(const std::string& tag, const std::string& msg) {
        s_logger->log(spdlog::level::err, "[{}] {}", tag, msg);
    }

    template<typename... Args>
    static void logd(const std::string& tag, const std::string& fmt_str, Args&&... args) {
        s_logger->log(spdlog::level::debug, "[{}] {}", tag, fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void logi(const std::string& tag, const std::string& fmt_str, Args&&... args) {
        s_logger->log(spdlog::level::info, "[{}] {}", tag, fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void logw(const std::string& tag, const std::string& fmt_str, Args&&... args) {
        s_logger->log(spdlog::level::warn, "[{}] {}", tag, fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void loge(const std::string& tag, const std::string& fmt_str, Args&&... args) {
        s_logger->log(spdlog::level::err, "[{}] {}", tag, fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    static void logendl() {
        s_logger->log(spdlog::level::info, "");
    }

    static spdlog::level::level_enum map_av_level(int av_level) {
        // AV_LOG_QUIET=-8, PANIC=0, FATAL=8, ERROR=16, WARNING=24, INFO=32, VERBOSE=40, DEBUG=48, TRACE=56
        if (av_level <= AV_LOG_PANIC)   return spdlog::level::critical;
        if (av_level <= AV_LOG_FATAL)   return spdlog::level::critical;
        if (av_level <= AV_LOG_ERROR)   return spdlog::level::err;
        if (av_level <= AV_LOG_WARNING) return spdlog::level::warn;
        if (av_level <= AV_LOG_INFO)    return spdlog::level::info;
#if LIBAVUTIL_VERSION_MAJOR >= 58
        if (av_level <= AV_LOG_VERBOSE) return spdlog::level::debug;
        if (av_level <= AV_LOG_DEBUG)   return spdlog::level::debug;
        return spdlog::level::trace;
#else
        // 老版本没有 TRACE
        return spdlog::level::debug;
#endif
    }

    // 供 av_log_set_callback 使用的回调
    static void ffmpeg_log_callback(void* ptr, int level, const char* fmt, va_list vl) {
        if (!s_logger) return;

        // 过滤：如果 FFmpeg 的级别高于我们想看的，就丢弃
        if (level >av_log_get_level()) {
            return;
        }
        auto mapped = map_av_level(level);
        if (mapped < s_logger->level()) return;

        // 组装一整行（FFmpeg 可能分多次调用输出同一行）
        char line[1024];
        int print_prefix = 1; // 让 av_log_format_line 在适当时机加前缀
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 0, 0)
        av_log_format_line2(ptr, level, fmt, vl, line, sizeof(line), &print_prefix);
#else
        av_log_format_line(ptr, level, fmt, vl, line, sizeof(line), &print_prefix);
#endif

        // 使用线程本地缓冲把“半行”拼成整行
        thread_local std::string tl_buf;
        tl_buf += line;

        // FFmpeg 一般以 '\n' 结束一条完整消息
        size_t pos;
        while ((pos = tl_buf.find('\n')) != std::string::npos) {
            std::string one = tl_buf.substr(0, pos);
            tl_buf.erase(0, pos + 1);
            // 去掉可能的尾部 '\r'
            if (!one.empty() && one.back() == '\r') one.pop_back();

            // 真正写到 spdlog
            s_logger->log(mapped, "{}", one);
        }
    }

private:
    static std::shared_ptr<spdlog::logger> s_logger;
};