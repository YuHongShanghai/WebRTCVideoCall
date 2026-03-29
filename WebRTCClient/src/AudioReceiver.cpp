//
// Created by 余泓 on 2025/11/13.
//

#include "AudioReceiver.h"

#include <QDebug>

#include "Logger.h"
#include "util.h"

AudioReceiver::AudioReceiver(std::function<void(AVFrame *)> callback): frameCallback_(callback) {

}

AudioReceiver::~AudioReceiver() {
    if (running_) {
        stop();
    }
}

int AudioReceiver::init(int rtpPort) {
    Logd("listening on {}", rtpPort);
    avformat_network_init();
    std::string sdp =
        "v=0\r\n"
        "o=- 0 0 IN IP4 127.0.0.1\r\n"
        "c=IN IP4 127.0.0.1\r\n"
        "t=0 0\r\n"
        "m=audio " + std::to_string(rtpPort) + " RTP/AVP 111\r\n"
        "a=rtpmap:111 opus/48000/2\r\n";
    unsigned char* buffer = (unsigned char*)av_malloc(sdp.size());
    memcpy(buffer, sdp.data(), sdp.size());
    AVIOContext *pb = avio_alloc_context(buffer, sdp.size(), 0, nullptr, nullptr, nullptr, nullptr);
    if (pb == nullptr) {
        Loge("avio_alloc_context failed");
        return -1;
    }
    formatCtx_ = avformat_alloc_context();
    formatCtx_->pb = pb;
    formatCtx_->interrupt_callback.callback = &AudioReceiver::readInterruptCbStatic;
    formatCtx_->interrupt_callback.opaque = this;

    AVDictionary *options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file,udp,rtp", 0);
    av_dict_set(&options, "buffer_size", "2048", 0);

    int ret = avformat_open_input(&formatCtx_, nullptr, nullptr, &options);
    if (ret < 0) {
        Loge("avformat_open_input failed: {}", av_errstr(ret));
        av_freep(&pb->buffer);
        avio_context_free(&pb);
        avformat_free_context(formatCtx_);
        return -1;
    }

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
    if (codec == nullptr) {
        Loge("avcodec_find_decoder failed");
        return -1;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        Loge("avcodec_alloc_context3 failed");
        return -1;
    }
    codecCtx_->codec_type = AVMEDIA_TYPE_AUDIO;
    codecCtx_->sample_rate = 48000;

    if (avcodec_open2(codecCtx_, codec, nullptr) != 0) {
        Loge("avcodec_open2 {} failed", codec->name);
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
        return -1;
    }

    qDebug() << "Decoder sample_fmt:"
                 << av_get_sample_fmt_name(codecCtx_->sample_fmt);
    qDebug() << "Decoder sample_rate:" << codecCtx_->sample_rate;
    qDebug() << "Decoder channels:" << codecCtx_->ch_layout.nb_channels;

    Logi("AudioReceiver initialized successfully.");
    return 0;
}

void AudioReceiver::start() {
    if (running_) {
        return;
    }
    running_ = true;
    receiveThread_ = new std::thread(&AudioReceiver::receiveLoop, this);
}

void AudioReceiver::stop() {
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

void AudioReceiver::receiveLoop() {
    audioPkt_ = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    Logd("enter loop");
    int ret;
    while (running_) {
        ret = av_read_frame(formatCtx_, audioPkt_);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            Loge("av_read_frame failed: {}", av_errstr(ret));
            break;
        }

        ret = avcodec_send_packet(codecCtx_, audioPkt_);
        av_packet_unref(audioPkt_);
        if (ret < 0) {
            Loge("avcodec_send_packet failed: {}", av_errstr(ret));
            continue;
        }

        // 取出 PCM
        while (ret >= 0) {
            ret = avcodec_receive_frame(codecCtx_, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                Loge("avcodec_receive_frame failed: {}", av_errstr(ret));
                break;
            }

            // opus解码后的音频格式是 AV_SAMPLE_FMT_FLTP
            if (frameCallback_) {
                frameCallback_(frame);
            }
        }
    }
    av_packet_free(&audioPkt_);
    avformat_close_input(&formatCtx_);
    av_frame_free(&frame);
}

int AudioReceiver::readInterruptCbStatic(void *opaque) {
    AudioReceiver *self = (AudioReceiver *)opaque;
    return self->readInterruptCb();
}

int AudioReceiver::readInterruptCb() {
    return running_ ? 0 : 1;
}
