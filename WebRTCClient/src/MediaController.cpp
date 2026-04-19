#include "MediaController.h"

#include "Logger.h"

extern "C" {
#include <libavutil/frame.h>
}

// ── 蹦床函数（C 函数指针 → 成员函数，跨 ABI 安全）──────────

void MediaController::localFrameTrampoline(AVFrame* frame, void* ctx) {
    static_cast<MediaController*>(ctx)->recvLocalVideoFrame(frame);
}

// ── MediaController ───────────────────────────────────────

MediaController::MediaController(QObject *parent) : QObject(parent) {
    videoProcesser_ = std::make_unique<VideoProcesser>();
    videoCapturer_  = std::make_unique<VideoCapturer>(localFrameTrampoline, this);
}

MediaController::~MediaController() {
    stopCaptureVideo();
}

void MediaController::startCaptureVideo(std::function<void(AVFrame *)> webrtcSink) {
    // 更新 WebRTC 回调（即使已在运行也可安全调用）。
    // WebRTC 的投递在 recvLocalVideoFrame 中完成（分割后），因此不再使用
    // VideoCapturer 的 webrtcCallback_ 直通路径。
    if (webrtcSink) {
        webrtcSink_ = std::move(webrtcSink);
    }

    // 已在运行则无需重新 init/start
    if (videoCapturer_->isRunning()) return;

    if (videoCapturer_->init() < 0) {
        Loge("Failed to init VideoCapturer");
        return;
    }
    videoCapturer_->start();
}

void MediaController::stopCaptureVideo() {
    videoCapturer_->stop();
}

void MediaController::startGesture() {
    videoProcesser_->enableGestureDetection(true);
}

void MediaController::stopGesture() {
    videoProcesser_->enableGestureDetection(false);
}

void MediaController::setBgEnabled(bool enabled) {
    videoProcesser_->enableSegmentation(enabled);
}

void MediaController::recvLocalVideoFrame(AVFrame *frame) {
    // 手势识别（不修改帧内容，使用原始帧）
    Detection det = videoProcesser_->gestureRecognition(frame);
    if (!det.label.empty()) {
        emit localGestureResult(det);
    }

    // 背景分割：分割结果写入新帧后 emit，原帧释放
    AVFrame* outFrame = av_frame_alloc();
    videoProcesser_->segmentation(frame, outFrame);
    av_frame_free(&frame);

    // 把分割后的帧也推给 WebRTC 编码端，保证远端看到的是替换背景后的画面。
    // webrtcSink_ 接管帧所有权；本地预览通过 onLocalVideoFrame 使用 outFrame。
    if (webrtcSink_) {
        AVFrame* forWebrtc = av_frame_alloc();
        av_frame_ref(forWebrtc, outFrame);
        webrtcSink_(forWebrtc);
    }

    emit onLocalVideoFrame(outFrame);
}
