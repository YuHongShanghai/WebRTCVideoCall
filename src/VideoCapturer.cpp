#include "VideoCapturer.h"
#include <qtextstream.h>
#include "Logger.h"

#include "ClientInfo.h"
#include "magic_enum.hpp"

VideoCapturer::VideoCapturer(std::function<void(AVFrame*)> callback): frameCallback_(callback) {}

VideoCapturer::~VideoCapturer() {
    stop();
}

int VideoCapturer::init(int rtpPort) {
    Logd("rtpPort {}", rtpPort);
    avdevice_register_all();

    // 1️⃣ 打开摄像头输入
    const AVInputFormat* inputFmt = av_find_input_format("avfoundation");
    if (!inputFmt) {
        Loge("Cannot find avfoundation input format");
        return -1;
    }

    AVDictionary* options = nullptr;
    av_dict_set(&options, "framerate", "30", 0);
    av_dict_set(&options, "video_size", "1280x720", 0);
    av_dict_set(&options, "pixel_format", "nv12", 0);

    if (avformat_open_input(&inFmtCtx_, "0", inputFmt, &options) < 0
        && avformat_open_input(&inFmtCtx_, "1", inputFmt, &options)) {
        Loge("Cannot open camera");
        return -1;
    }
    Logd("camera opened, url {}",inFmtCtx_->url);
    av_dict_free(&options);

    if (avformat_find_stream_info(inFmtCtx_, nullptr) < 0) {
        Loge("Cannot find stream info");
        return -1;
    }

    videoStreamIndex_ = av_find_best_stream(inFmtCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex_ < 0) {
        Loge("Cannot find video stream");
        return -1;
    }

    AVStream* inStream = inFmtCtx_->streams[videoStreamIndex_];
    AVCodecParameters* inCodecPar = inStream->codecpar;

    const AVCodec* decoder = avcodec_find_decoder(inCodecPar->codec_id);

    decCtx_ = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decCtx_, inCodecPar);
    if (avcodec_open2(decCtx_, decoder, nullptr) < 0) {
        Loge("failed to open decoder");
        return -1;
    }

    Logi("Input video: {}x{} format={}", inCodecPar->width, inCodecPar->height,
         av_get_pix_fmt_name((AVPixelFormat)inCodecPar->format));

    // 2️⃣ 初始化编码器 (H.264)
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        Loge("H.264 encoder not found");
        return -1;
    }

    int fps = 30;
    encCtx_ = avcodec_alloc_context3(codec);
    encCtx_->width = inCodecPar->width;
    encCtx_->height = inCodecPar->height;
    encCtx_->time_base = {1, fps};
    encCtx_->framerate = {fps, 1};
    encCtx_->pix_fmt = AV_PIX_FMT_YUV420P;
    encCtx_->bit_rate = 2000000;
    encCtx_->gop_size = fps;

    av_opt_set(encCtx_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(encCtx_->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(encCtx_, codec, nullptr) < 0) {
        Loge("Failed to open encoder");
        return -1;
    }

    // 3️⃣ 创建输出 RTP
    auto outputUrl = std::string("rtp://127.0.0.1:") + std::to_string(rtpPort);
    if (avformat_alloc_output_context2(&outFmtCtx_, nullptr, "rtp", outputUrl.c_str()) < 0) {
        Loge("Failed to create RTP context");
        return -1;
    }

    outStream_ = avformat_new_stream(outFmtCtx_, codec);
    avcodec_parameters_from_context(outStream_->codecpar, encCtx_);
    outStream_->time_base = encCtx_->time_base;

    if (!(outFmtCtx_->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outFmtCtx_->pb, outputUrl.c_str(), AVIO_FLAG_WRITE) < 0) {
            Loge("Failed to open output URL");
            return -1;
        }
    }

    if (avformat_write_header(outFmtCtx_, nullptr) < 0) {
        Loge("Failed to write header");
        return -1;
    }

    // 4️⃣ 初始化像素转换
    swsCtx_ = sws_getContext(inCodecPar->width, inCodecPar->height,
                            (AVPixelFormat)inCodecPar->format,
                            encCtx_->width, encCtx_->height, encCtx_->pix_fmt,
                            SWS_BICUBIC, nullptr, nullptr, nullptr);

    yuvFrame_ = av_frame_alloc();
    yuvFrame_->format = encCtx_->pix_fmt;
    yuvFrame_->width = encCtx_->width;
    yuvFrame_->height = encCtx_->height;
    av_frame_get_buffer(yuvFrame_, 32);

    Logi("VideoCapturer initialized successfully.");
    return 0;
}

void VideoCapturer::capture() {
    AVPacket *inPkt = av_packet_alloc();
    AVPacket *outPkt = av_packet_alloc();
    AVFrame *decFrame = av_frame_alloc();
    int frameIndex = 0;

    Logi("Start capturing and sending RTP...");

    while (running_) {
        int ret = av_read_frame(inFmtCtx_, inPkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                av_usleep(10000); // 休眠10ms再试
                continue;
            } else if (ret == AVERROR_EOF) {
                Logi("End of camera stream");
                break;
            } else {
                Loge("av_read_frame failed: {}", av_err2str(ret));
                break;
            }
        }

        if (inPkt->stream_index != videoStreamIndex_) {
            av_packet_unref(inPkt);
            continue;
        }

        // 解码 raw 帧（avfoundation 输出 rawvideo）
        avcodec_send_packet(decCtx_, inPkt);

        while (avcodec_receive_frame(decCtx_, decFrame) == 0) {
            std::lock_guard lock(gestureMutex_);
            sws_scale(swsCtx_, decFrame->data, decFrame->linesize, 0, decCtx_->height, yuvFrame_->data,
                      yuvFrame_->linesize);
            yuvFrame_->pts = frameIndex++;

            if (frameCallback_) {
                frameCallback_(yuvFrame_);
            }

            // 编码
            if (avcodec_send_frame(encCtx_, yuvFrame_) == 0) {
                while (avcodec_receive_packet(encCtx_, outPkt) == 0) {
                    outPkt->stream_index = outStream_->index;
                    av_interleaved_write_frame(outFmtCtx_, outPkt);
                    av_packet_unref(outPkt);
                }
            }
            av_frame_unref(decFrame);
        }
        av_packet_unref(inPkt);
    }

    av_packet_free(&inPkt);
    av_packet_free(&outPkt);
    av_frame_free(&decFrame);
}

void VideoCapturer::gestureRecognition() {
    AVFrame *gestureFrame = nullptr;
    while (gestureThreadRunning_) {
        if (yuvFrame_) {
            std::lock_guard lock(gestureMutex_);
            gestureFrame = av_frame_clone(yuvFrame_);
            av_frame_get_buffer(gestureFrame, 0);
            av_frame_copy(gestureFrame, yuvFrame_);
        }
        auto result = processer_.gestureRecognition(gestureFrame);
        if (gestureFrame) {
            av_frame_free(&gestureFrame);
            gestureFrame = nullptr;
        }

        if (!result.label.empty() && gestureCallback_) {
            gestureCallback_(result);
        }
    }
}

void VideoCapturer::start() {
    if (running_) {
        return;
    }
    running_ = true;
    captureThread_ = new std::thread(&VideoCapturer::capture, this);
}

void VideoCapturer::stop() {
    Logd("start");
    if (!running_) {
        return;
    }
    running_ = false;
    if (captureThread_ && captureThread_->joinable()) {
        Logd("waiting for capture thread to stop");
        captureThread_->join();
    }
    captureThread_ = nullptr;

    if (gestureThread_ && gestureThread_->joinable()) {
        Logd("waiting for process thread to stop");
        gestureThread_->join();
    }
    gestureThread_ = nullptr;

    av_write_trailer(outFmtCtx_);

    av_frame_free(&yuvFrame_);
    sws_freeContext(swsCtx_);
    avcodec_free_context(&decCtx_);
    avcodec_free_context(&encCtx_);
    avformat_close_input(&inFmtCtx_);

    if (!(outFmtCtx_->oformat->flags & AVFMT_NOFILE))
        avio_close(outFmtCtx_->pb);
    avformat_free_context(outFmtCtx_);

    Logi("VideoCapturer stopped.");
}

void VideoCapturer::setGestureCb(const std::function<void(Detection &)> &callback) { gestureCallback_ = callback; }

void VideoCapturer::startGesture() {
    processer_.enableGestureDetection(true);
    gestureThreadRunning_ = true;
    gestureThread_ = new std::thread(&VideoCapturer::gestureRecognition, this);
}

void VideoCapturer::stopGesture() {
    gestureThreadRunning_ = false;
    if (gestureThread_ && gestureThread_->joinable()) {
        Logd("waiting for process thread to stop");
        gestureThread_->join();
    }
    gestureThread_ = nullptr;
    processer_.enableGestureDetection(false);
}
