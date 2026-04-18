#pragma once

// 摄像头采集器：使用 FFmpeg 的 avfoundation 输入采集原始帧（NV12→YUV420P 转换）。
// 相比直接使用 AVFoundation Objective-C API，FFmpeg 抽象层在 macOS/Linux/Windows
// 上具有更好的兼容性，且消除了同 TU 下 AVFoundation/FFmpeg AVMediaType 的命名冲突。
// AI 处理（背景分割、手势识别）由调用方（MediaController）负责。

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <thread>

class VideoCapturer {
public:
    // frameCallback: 每帧回调，callee 拥有 AVFrame*，负责 av_frame_free
    explicit VideoCapturer(void (*frameCallback)(AVFrame*, void* ctx), void* ctx);
    ~VideoCapturer();

    // 初始化摄像头（已初始化时幂等返回 0）
    int init();

    void start();
    void stop();

    bool isRunning() const;

    // WebRTC 帧注入回调（可选）：每帧同时调用，callee 负责释放帧
    void setWebRTCCallback(void (*cb)(AVFrame*, void* ctx), void* ctx);

private:
    void captureLoop();

    using FrameCb = void (*)(AVFrame*, void*);

    FrameCb           frameCallback_  = nullptr;
    void*             frameCtx_       = nullptr;
    FrameCb           webrtcCallback_ = nullptr;
    void*             webrtcCtx_      = nullptr;

    AVFormatContext*  inFmtCtx_         = nullptr;
    AVCodecContext*   decCtx_           = nullptr;
    SwsContext*       swsCtx_           = nullptr;
    int               videoStreamIndex_ = -1;
    int               width_            = 0;
    int               height_           = 0;

    std::thread       captureThread_;
    std::atomic<bool> running_{false};
    int64_t           frameIndex_ = 0;
};
