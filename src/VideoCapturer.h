//
// Created by 余泓 on 2025/11/2.
//

#ifndef VIDEOCAPTURER_H
#define VIDEOCAPTURER_H
#include "VideoProcesser.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
}
#include <thread>

class VideoCapturer {
public:
    VideoCapturer(std::function<void(AVFrame*)> callback);
    ~VideoCapturer();
    int init(int rtpPort);
    void start();
    void stop();
    void setGestureCb(const std::function<void(Detection&)> &callback);
    void startGesture();
    void stopGesture();
    void setBgEnabled(bool enabled);

private:
    void capture();
    void gestureRecognition();

    AVFormatContext *inFmtCtx_  = nullptr;
    AVCodecContext* decCtx_ = nullptr;
    SwsContext *swsCtx_ = nullptr;
    AVFrame *yuvFrame_ = nullptr;
    AVCodecContext *encCtx_ = nullptr;
    AVStream *outStream_ = nullptr;
    AVFormatContext *outFmtCtx_ = nullptr;
    int videoStreamIndex_;
    std::thread *captureThread_ = nullptr;
    std::atomic<bool> running_;
    std::function<void(AVFrame*)> frameCallback_;

    VideoProcesser processer_;

    std::function<void(Detection&)> gestureCallback_ = nullptr;
    std::thread *gestureThread_ = nullptr;
    std::mutex gestureMutex_;
    std::atomic<bool> gestureThreadRunning_;
};



#endif //VIDEOCAPTURER_H
