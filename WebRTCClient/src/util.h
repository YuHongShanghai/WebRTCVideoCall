#ifndef UTIL_H
#define UTIL_H

#include <memory>
#include <string>

extern "C" {
#include <libavutil/frame.h>
}

int64_t getTimeMillisecond();
std::string av_errstr(int errnum);

// unique_ptr deleter，用于在异步边界（Qt QueuedConnection 等）上
// 自动释放 AVFrame —— 若 lambda 因线程退出等原因未执行，
// unique_ptr 析构会兜底 av_frame_free，防止帧内存泄漏。
struct AVFrameDeleter {
    void operator()(AVFrame* f) const noexcept {
        if (f) av_frame_free(&f);
    }
};
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

#endif //UTIL_H
