#pragma once

#include <functional>
#include <memory>
#include <QObject>

#include "GestureInfer.h"
#include "VideoProcesser.h"
#include "VideoCapturer.h"

// MediaController 管理本地视频采集（摄像头）及 AI 处理。
// VideoCapturer 负责原始帧采集（libwebrtc VideoCaptureModule），
// VideoProcesser 负责手势识别/背景分割（ONNX Runtime + OpenCV），
// 两者均在此处协调，AI 部分由 Apple clang 编译（无 -fno-exceptions 限制）。
//
// 通过静态蹦床函数将 VideoCapturer 的 C 函数指针回调桥接到成员函数，
// 避免 std::function 跨 ABI（std::__1 vs std::__Cr）边界传递。
class MediaController : public QObject {
    Q_OBJECT
public:
    explicit MediaController(QObject *parent = nullptr);
    ~MediaController();

    // 启动/停止摄像头采集
    // webrtcSink：每帧同时推给 libwebrtc（可为空）
    void startCaptureVideo(std::function<void(AVFrame *)> webrtcSink = nullptr);
    void stopCaptureVideo();

    // 手势识别控制
    void startGesture();
    void stopGesture();

    // 背景分割控制
    void setBgEnabled(bool enabled);

signals:
    void onLocalVideoFrame(AVFrame *frame);   // 本地摄像头帧（显示用）
    void localGestureResult(Detection result);

private:
    void recvLocalVideoFrame(AVFrame *frame);

    // C 蹦床函数（作为 VideoCapturer 回调，ctx = this）
    static void localFrameTrampoline(AVFrame* frame, void* ctx);

    std::unique_ptr<VideoCapturer>  videoCapturer_;
    std::unique_ptr<VideoProcesser> videoProcesser_;
    std::function<void(AVFrame*)>   webrtcSink_;  // 保持 webrtcSink 生命周期
};
