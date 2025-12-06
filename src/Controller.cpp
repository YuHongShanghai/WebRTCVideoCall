#include "Controller.h"

#include "Logger.h"
#include "magic_enum.hpp"
#include "util.h"

Controller::Controller(QObject *parent) : QObject(parent) {
    client_ = new ClientWorker();
    clientThread_ = new QThread;
    client_->moveToThread(clientThread_);
    connect(client_, &ClientWorker::remoteJoined, this, &Controller::onRemoteJoined, Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteLeft, this, &Controller::onRemoteLeft, Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteIds, this, &Controller::onRemoteIds, Qt::QueuedConnection);
    connect(client_, &ClientWorker::pcStateChanged, this, &Controller::onPcStateChanged, Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteCall, this, &Controller::onRemoteCall, Qt::QueuedConnection);
    connect(client_, &ClientWorker::pcClosed, this, &Controller::onPcClosed, Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteMessage, this, &Controller::remoteMessage, Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteVideoEnabled, this, &Controller::onRemoteVideoEnabled, Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteAudioEnabled, this, &Controller::onRemoteAudioEnabled, Qt::QueuedConnection);

    clientThread_->start();

    QMetaObject::invokeMethod(client_, "init", Qt::QueuedConnection);
    mediaController_ = new MediaController(this);
    connect(mediaController_, &MediaController::onRemoteVideoFrame, this, &Controller::onRemoteVideoFrame);
    connect(mediaController_, &MediaController::onLocalVideoFrame, this, &Controller::onLocalVideoFrame);

    connect(mediaController_, &MediaController::onRemoteAudioFrame, this, &Controller::onRemoteAudioFrame);

    audioPlayer_ = new AudioPlayer(this);
}

Controller::~Controller() {
    stopMediaTransport();

    clientThread_->quit();
    clientThread_->wait();
    client_->deleteLater();
    clientThread_->deleteLater();

    if (yuvFrame_) {
        av_frame_unref(yuvFrame_);
        av_frame_free(&yuvFrame_);
    }
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
    }
    if (swrCtx_) {
        swr_free(&swrCtx_);
    }
}

QString Controller::localId() const { return client_->getLocalId(); }

bool Controller::videoEnabled() const {
    return videoEnabled_;
}

void Controller::setVideoEnabled(bool enabled) {
    if (enabled != videoEnabled_) {
        videoEnabled_ = enabled;
        emit videoEnabledChanged(videoEnabled_);
        if (enabled) {
            mediaController_->startCaptureVideo(client_->getVideoSrcPort());
        } else {
            mediaController_->stopCaptureVideo();
        }
        client_->notifyVideoEnabled(enabled);
    }
}

bool Controller::remoteVideoEnabled() const { return remoteVideoEnabled_; }

bool Controller::audioEnabled() const {
    return audioEnabled_;
}

void Controller::setAudioEnabled(bool enabled) {
    if (enabled != audioEnabled_) {
        audioEnabled_ = enabled;
        emit audioEnabledChanged(audioEnabled_);
        if (enabled) {
            mediaController_->startCaptureAudio(client_->getAudioSrcPort());
        } else {
            mediaController_->stopCaptureAudio();
        }
        client_->notifyAudioEnabled(enabled);
    }
}

void Controller::onRemoteJoined(QString id) {
    emit remoteJoined(id);
}

void Controller::onRemoteLeft(QString id) {
    emit remoteLeft(id);
}

void Controller::onRemoteIds(QStringList ids) {
    emit remoteIds(ids);
}

void Controller::onPcStateChanged(rtc::PeerConnection::State state) {
    emit pcStateChanged(static_cast<PcState>(state));
    Logd("onPcStateChanged {}", magic_enum::enum_name(state));
    if (state == rtc::PeerConnection::State::Connected) {
        startMediaTransport();
    } else if (state == rtc::PeerConnection::State::Closed) {
        stopMediaTransport();
    }
}

void Controller::onRemoteCall(QString id) { emit remoteCall(id); }

void Controller::onPcClosed(QString id) {
    stopMediaTransport();
    emit pcClosed(id);
}

void Controller::startMediaTransport() {
    localVideoWidth_ = 0;
    localVideoHeight_ = 0;
    remoteVideoWidth_ = 0;
    remoteVideoHeight_ = 0;
    mediaController_->startCaptureVideo(client_->getVideoSrcPort());
    mediaController_->startReceiveVideo(client_->getVideoSinkPort());
    mediaController_->startCaptureAudio(client_->getAudioSrcPort());
    mediaController_->startReceiveAudio(client_->getAudioSinkPort());
}

void Controller::stopMediaTransport() {
    Logd("start");
    mediaController_->stopCaptureVideo();
    mediaController_->stopReceiveVideo();
    mediaController_->stopCaptureAudio();
    mediaController_->stopReceiveAudio();
    Logd("end");
}

void Controller::updateSwsContext(int width, int height, int format) {
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
    }
    swsCtx_ = sws_getContext(width, height, (enum AVPixelFormat) format, width, height, AV_PIX_FMT_YUV420P,
                             SWS_BILINEAR, NULL, NULL, NULL);
    if (!swsCtx_) {
        qDebug() << "sws_getContext failed";
        return;
    }
    if (yuvFrame_) {
        av_frame_unref(yuvFrame_);
        av_frame_free(&yuvFrame_);
    }

    yuvFrame_ = av_frame_alloc();

    if (!yuvFrame_) {
        qDebug() << "av_frame_alloc yuv frame failed";
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
        return;
    }
    yuvFrame_->format = AV_PIX_FMT_YUV420P;
    yuvFrame_->width = width;
    yuvFrame_->height = height;
    if (av_frame_get_buffer(yuvFrame_, 0) < 0) {
        qDebug() << "av_frame_get_buffer yuv frame failed";
        av_frame_free(&yuvFrame_);
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
        yuvFrame_ = nullptr;
    }
}

void Controller::initSwrContext(AVFrame *frame) {
    swrCtx_ = swr_alloc();
    av_opt_set_chlayout(swrCtx_, "in_chlayout",  &frame->ch_layout, 0);
    av_opt_set_int      (swrCtx_, "in_sample_rate", frame->sample_rate, 0);
    av_opt_set_sample_fmt(swrCtx_, "in_sample_fmt", (AVSampleFormat)frame->format, 0);

    // out: 固定 S16 立体声
    AVChannelLayout outCh;
    av_channel_layout_from_mask(&outCh, AV_CH_LAYOUT_STEREO);
    av_opt_set_chlayout(swrCtx_, "out_chlayout", &outCh, 0);
    av_opt_set_int      (swrCtx_, "out_sample_rate", frame->sample_rate, 0);
    av_opt_set_sample_fmt(swrCtx_, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    int ret = swr_init(swrCtx_);
    if (ret < 0) {
        Loge("init swr_init failed: {}", av_errstr(ret));
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
        return;
    }

    swrBuffer_.resize(MAX_OUT_SAMPLES*2);
}

void Controller::callRemote(const QString &id) {
    QMetaObject::invokeMethod(client_, "call", Qt::QueuedConnection, Q_ARG(QString, id));
}

void Controller::hungup() {
    stopMediaTransport();
    QMetaObject::invokeMethod(client_, "hungup", Qt::QueuedConnection);
}

void Controller::onRemoteVideoFrame(AVFrame *frame) {
    if (remoteVideoWidth_ != frame->width || remoteVideoHeight_ != frame->height) {
        remoteVideoWidth_ = frame->width;
        remoteVideoHeight_ = frame->height;
        Logd("videoSizeChanged: {} x {}", remoteVideoWidth_, remoteVideoHeight_);
        updateSwsContext(remoteVideoWidth_, remoteVideoHeight_, frame->format);
        emit remoteVideoSizeChanged(remoteVideoWidth_, remoteVideoHeight_);
    }
    YUVData yuvData;
    if (frame->format == AV_PIX_FMT_YUV420P) {
        extractYUVFromAVFrame(frame, yuvData);
    } else {
        sws_scale(swsCtx_, (const uint8_t *const *) frame->data, frame->linesize, 0, remoteVideoHeight_, yuvFrame_->data,
                  yuvFrame_->linesize);
        extractYUVFromAVFrame(yuvFrame_, yuvData);
    }
    emit receiveRemoteYuvData(yuvData);
    av_frame_unref(frame);
    av_frame_free(&frame);
}

void Controller::onLocalVideoFrame(AVFrame *frame) {
    if (localVideoWidth_ != frame->width || localVideoHeight_ != frame->height) {
        localVideoWidth_ = frame->width;
        localVideoHeight_ = frame->height;
        Logd("videoSizeChanged: {} x {}", localVideoWidth_, localVideoHeight_);
        emit localVideoSizeChanged(localVideoWidth_, localVideoHeight_);
    }
    YUVData yuvData;
    if (frame->format == AV_PIX_FMT_YUV420P) {
        extractYUVFromAVFrame(frame, yuvData);
        emit receiveLocalYuvData(yuvData);
    }
    av_frame_unref(frame);
    av_frame_free(&frame);
}

void Controller::extractYUVFromAVFrame(AVFrame *frame, YUVData &yuv) {
    // 提取行大小和高度
    yuv.yLineSize = frame->linesize[0];
    yuv.uLineSize = frame->linesize[1];
    yuv.vLineSize = frame->linesize[2];
    yuv.height = frame->height;

    int chromaHeight = frame->height / 2;

    // 拷贝 Y 分量
    yuv.Y.resize(yuv.yLineSize * frame->height);
    for (int i = 0; i < frame->height; ++i) {
        memcpy(yuv.Y.data() + i * yuv.yLineSize, frame->data[0] + i * frame->linesize[0], yuv.yLineSize);
    }

    // 拷贝 U 分量
    yuv.U.resize(yuv.uLineSize * chromaHeight);
    for (int i = 0; i < chromaHeight; ++i) {
        memcpy(yuv.U.data() + i * yuv.uLineSize, frame->data[1] + i * frame->linesize[1], yuv.uLineSize);
    }

    // 拷贝 V 分量
    yuv.V.resize(yuv.vLineSize * chromaHeight);
    for (int i = 0; i < chromaHeight; ++i) {
        memcpy(yuv.V.data() + i * yuv.vLineSize, frame->data[2] + i * frame->linesize[2], yuv.vLineSize);
    }
}

void Controller::onRemoteAudioFrame(AVFrame *frame) {
    if (swrCtx_ == nullptr) {
        initSwrContext(frame);
    }

    if (swrCtx_ == nullptr) {
        Loge("swrCtx is null");
        return;
    }

    uint8_t *out_data[1];
    out_data[0] = reinterpret_cast<uint8_t *>(swrBuffer_.data());

    int converted = swr_convert(swrCtx_, out_data, MAX_OUT_SAMPLES, (const uint8_t **) frame->data, frame->nb_samples);
    if (converted < 0) {
        Loge("swr_convert error");
        return;
    }

    if (converted == 0)
        return;

    int outChannels = 2;
    int bytes = converted * outChannels * 2;
    audioPlayer_->pushAudio(reinterpret_cast<const char *>(swrBuffer_.data()), bytes);
}

void Controller::onRemoteVideoEnabled(bool enabled) {
    if (enabled != remoteVideoEnabled_) {
        remoteVideoEnabled_ = enabled;
        emit remoteVideoEnabledChanged(remoteVideoEnabled_);
        if (enabled) {
            mediaController_->startReceiveVideo(client_->getVideoSinkPort());
        } else {
            mediaController_->stopReceiveVideo();
        }
    }
}

void Controller::onRemoteAudioEnabled(bool enabled) {
    if (enabled != remoteAudioEnabled_) {
        remoteAudioEnabled_ = enabled;
        emit remoteAudioEnabledChanged(remoteAudioEnabled_);
        if (enabled) {
            mediaController_->startReceiveAudio(client_->getAudioSinkPort());
        } else {
            mediaController_->stopReceiveAudio();
        }
    }
}

void Controller::initVideoItem(QObject *mainWindow) {
    if (!mainWindow) {
        Loge("mainWindow is null");
        return;
    }

    QObject *remoteVideoItem = mainWindow->findChild<QObject *>("remoteVideo");
    if (remoteVideoItem == nullptr) {
        Loge("remoteVideoItem is null");
        return;
    }

    connect(this, SIGNAL(remoteVideoSizeChanged(int, int)), remoteVideoItem, SLOT(onVideoSizeChanged(int, int)),
            Qt::UniqueConnection);
    connect(this, SIGNAL(receiveRemoteYuvData(const YUVData &)), remoteVideoItem,
            SLOT(onReceiveVideoData(const YUVData &)), Qt::UniqueConnection);

    QObject *localVideoItem = mainWindow->findChild<QObject *>("localVideo");
    if (localVideoItem == nullptr) {
        Loge("localVideoItem is null");
        return;
    }

    connect(this, SIGNAL(localVideoSizeChanged(int, int)), localVideoItem, SLOT(onVideoSizeChanged(int, int)),
            Qt::UniqueConnection);
    connect(this, SIGNAL(receiveLocalYuvData(const YUVData &)), localVideoItem,
            SLOT(onReceiveVideoData(const YUVData &)), Qt::UniqueConnection);
}

void Controller::sendMessage(QString message) {
    client_->sendMessage(message);
}


