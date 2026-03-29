//
// Created by 余泓 on 2025/11/13.
//

#ifndef AUDIORECEIVER_H
#define AUDIORECEIVER_H
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <functional>
#include <thread>


class AudioReceiver {
public:
    AudioReceiver(std::function<void(AVFrame*)> callback);
    ~AudioReceiver();
    int init(int rtpPort);
    void start();
    void stop();
    void receiveLoop();

private:
    static int readInterruptCbStatic(void *opaque);
    int readInterruptCb();

    std::function<void(AVFrame*)> frameCallback_;
    std::thread *receiveThread_;
    std::atomic<bool> running_;
    AVFormatContext *formatCtx_ = nullptr;
    AVPacket *audioPkt_ = nullptr;
    AVCodecContext *codecCtx_ = nullptr;
    int streamIdx_ = -1;
};



#endif //AUDIORECEIVER_H
