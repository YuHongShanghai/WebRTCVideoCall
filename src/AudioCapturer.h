//
// Created by 余泓 on 2025/11/9.
//

#ifndef AUDIOCAPTURER_H
#define AUDIOCAPTURER_H
#include "AudioProcesser.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libavutil/time.h>
}
#include <thread>

class AudioCapturer {
public:
    AudioCapturer();
    ~AudioCapturer();
    int init(int rtpPort);
    void capture();
    void start();
    void stop();

private:
    AVFormatContext *inFmtCtx_  = nullptr;
    AVCodecContext* decCtx_ = nullptr;
    SwrContext *swrCtx_ = nullptr;
    AVFrame *accFrame = nullptr;
    AVCodecContext *encCtx_ = nullptr;
    AVStream *outStream_ = nullptr;
    AVFormatContext *outFmtCtx_ = nullptr;
    int audioStreamIndex_;
    std::thread *captureThread_ = nullptr;
    std::atomic<bool> running_;
    const int OPUS_FRAME_SIZE = 960; // 20ms @ 48kHz

    std::unique_ptr<AudioProcesser> audioProcesser_;
};



#endif //AUDIOCAPTURER_H
