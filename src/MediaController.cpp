//
// Created by 余泓 on 2025/11/8.
//

#include "MediaController.h"

#include "ClientInfo.h"
#include "Controller.h"
#include "Logger.h"

MediaController::MediaController(QObject *parent): QObject(parent) {
    videoCapturer_ = std::make_unique<VideoCapturer>(std::bind(&MediaController::recvLocalVideoFrame, this, std::placeholders::_1));
    videoReceiver_ = std::make_unique<VideoReceiver>(std::bind(&MediaController::recvRemoteVideoFrame, this, std::placeholders::_1));
    audioCapturer_ = std::make_unique<AudioCapturer>();
    audioReceiver_ = std::make_unique<AudioReceiver>(std::bind(&MediaController::recvRemoteAudioFrame, this, std::placeholders::_1));
}

MediaController::~MediaController() {
    stopCaptureVideo();
    stopCaptureAudio();
    stopReceiveVideo();
    stopReceiveAudio();
}

void MediaController::startCaptureVideo(int dstPort) {
    if (videoCapturer_->init(dstPort) < 0) {
        Loge("failed to init video capturer");
        return;
    }

    videoCapturer_->start();

}
void MediaController::stopCaptureVideo() {
    videoCapturer_->stop();
}

void MediaController::startReceiveVideo(int port) {
    if (videoReceiver_->init(port) < 0) {
        Loge("failed to init video receiver");
        return;
    }
    videoReceiver_->start();
}

void MediaController::stopReceiveVideo() { videoReceiver_->stop(); }

void MediaController::startCaptureAudio(int port) {
    if (audioCapturer_->init(port) < 0) {
        Loge("failed to init audio capturer");
        return;
    }

    audioCapturer_->start();
}

void MediaController::stopCaptureAudio() { audioCapturer_->stop(); }

void MediaController::startReceiveAudio(int port) {
    if (audioReceiver_->init(port) < 0) {
        Loge("failed to init audio receiver");
        return;
    }
    audioReceiver_->start();
}

void MediaController::stopReceiveAudio() { audioReceiver_->stop(); }

void MediaController::recvRemoteVideoFrame(AVFrame *frame) {
    AVFrame *copy = av_frame_alloc();
    av_frame_ref(copy, frame);
    av_frame_make_writable(copy);
    emit onRemoteVideoFrame(copy);
}

void MediaController::recvLocalVideoFrame(AVFrame *frame) {
    AVFrame *copy = av_frame_alloc();
    av_frame_ref(copy, frame);
    av_frame_make_writable(copy);
    emit onLocalVideoFrame(copy);
}

void MediaController::recvRemoteAudioFrame(AVFrame *frame) {
    AVFrame *copy = av_frame_alloc();
    av_frame_ref(copy, frame);
    av_frame_make_writable(copy);
    emit onRemoteAudioFrame(copy);
}
