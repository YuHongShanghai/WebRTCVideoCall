#include "util.h"

#include <chrono>
extern "C" {
#include <libavutil/error.h>
}

int64_t getTimeMillisecond() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return millis;
}

std::string av_errstr(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    return std::string(av_make_error_string(buf, AV_ERROR_MAX_STRING_SIZE, errnum));
}