//
// Created by 余泓 on 2025/11/8.
//

#ifndef VIDEORECEIVER_H
#define VIDEORECEIVER_H
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <functional>
#include <thread>


class VideoReceiver {
public:
    VideoReceiver(std::function<void(AVFrame*)> callback);
    ~VideoReceiver();
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
    AVPacket *videoPkt_ = nullptr;
    AVCodecContext *codecCtx_ = nullptr;
    AVBufferRef* hwDeviceCtx_ = nullptr;
};



#endif //VIDEORECEIVER_H
