//
// Created by 余泓 on 2025/11/8.
//

#include "VideoReceiver.h"
#include "Logger.h"
#include "util.h"

VideoReceiver::VideoReceiver(std::function<void(AVFrame *)> callback): frameCallback_(callback) {

}

VideoReceiver::~VideoReceiver() {
    if (running_) {
        stop();
    }
}

int VideoReceiver::init(int rtpPort) {
    Logd("listening on {}", rtpPort);
    avformat_network_init();
    std::string sdp =
        "v=0\n"
        "m=video " + std::to_string(rtpPort) + " RTP/AVP 96\n"
        "c=IN IP4 127.0.0.1\n"
        "a=rtpmap:96 H264/90000\n";
    unsigned char* buffer = (unsigned char*)av_malloc(sdp.size());
    memcpy(buffer, sdp.data(), sdp.size());
    AVIOContext *pb = avio_alloc_context(buffer, sdp.size(), 0, nullptr, nullptr, nullptr, nullptr);
    if (pb == nullptr) {
        Loge("avio_alloc_context failed");
        return -1;
    }
    formatCtx_ = avformat_alloc_context();
    formatCtx_->pb = pb;
    formatCtx_->interrupt_callback.callback = &VideoReceiver::readInterruptCbStatic;
    formatCtx_->interrupt_callback.opaque = this;

    AVDictionary *options = nullptr;
    av_dict_set(&options, "stimeout", "500000", 0);
    av_dict_set(&options, "fflags", "nobuffer", 0);
    av_dict_set(&options, "protocol_whitelist", "file,udp,rtp", 0);
    av_dict_set(&options, "probesize", "100", 0);
    av_dict_set(&options, "analyzeduration", "0", 0);
    av_dict_set(&options, "buffer_size", "1024000", 0);
    av_dict_set(&options, "reorder_queue_size", "0", 0);

    int ret = avformat_open_input(&formatCtx_, nullptr, nullptr, &options);
    if (ret < 0) {
        Loge("avformat_open_input failed: {}", av_errstr(ret));
        av_freep(&pb->buffer);
        avio_context_free(&pb);
        avformat_free_context(formatCtx_);
        return -1;
    }

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
        Loge("avcodec_find_decoder failed");
        return -1;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        Loge("avcodec_alloc_context3 failed");
        return -1;
    }

    ret = av_hwdevice_ctx_create(&hwDeviceCtx_, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
    if (ret < 0) {
        Loge("failed to create specified HW device: {}", av_errstr(ret));
        return -1;
    }
    codecCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);

    if (avcodec_open2(codecCtx_, codec, nullptr) != 0) {
        Loge("avcodec_open2 {} failed", codec->name);
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
        return -1;
    }
    Logi("VideoReceiver initialized successfully.");
    return 0;
}

void VideoReceiver::start() {
    if (running_) {
        return;
    }
    running_ = true;
    receiveThread_ = new std::thread(&VideoReceiver::receiveLoop, this);
}

void VideoReceiver::stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    if (receiveThread_ && receiveThread_->joinable()) {
        receiveThread_->join();
    }
    receiveThread_ = nullptr;
    avformat_free_context(formatCtx_);
    avcodec_free_context(&codecCtx_);
}

void VideoReceiver::receiveLoop() {
    videoPkt_ = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *swFrame = av_frame_alloc();
    Logd("enter loop");
    while (running_) {
        while (av_read_frame(formatCtx_, videoPkt_) >= 0) {
            if (!running_) {
                av_packet_unref(videoPkt_);
                break;
            }

            // todo: remove to decode queue
            int ret;
            do {
                if (!running_) {
                    break;
                }

                ret = avcodec_receive_frame(codecCtx_, frame);
                if (ret == 0) {
                    int err;
                    // frame (AV_PIX_FMT_VIDEOTOOLBOX) -> swFrame(AV_PIX_FMT_NV12)
                    if ((err = av_hwframe_transfer_data(swFrame, frame, 0)) < 0) {
                        Loge("av_hwframe_transfer_data failed,: {}", av_errstr(err));
                        av_frame_unref(frame);
                        continue;
                    }
                    swFrame->pts = swFrame->best_effort_timestamp;
                    if (frameCallback_) {
                        frameCallback_(swFrame);
                    }
                    av_frame_unref(frame);
                    av_frame_unref(swFrame);
                }
            } while (ret == 0);

            if (!running_) {
                break;
            }

            if (avcodec_send_packet(codecCtx_, videoPkt_) != 0) {
                Logw("avcodec_send_packet failed");
            }

            av_packet_unref(videoPkt_);
        }
    }
    av_packet_free(&videoPkt_);
    avformat_close_input(&formatCtx_);
    av_frame_free(&frame);
    av_frame_free(&swFrame);
}

int VideoReceiver::readInterruptCbStatic(void *opaque) {
    VideoReceiver *self = (VideoReceiver *)opaque;
    return self->readInterruptCb();
}

int VideoReceiver::readInterruptCb() {
    return running_ ? 0 : 1;
}
