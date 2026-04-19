#include "Controller.h"

#include "ClientInfo.h"
#include "Logger.h"
#include "util.h"
#include "requestPermissions.h"

#include <QTimer>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

Controller::Controller(QObject *parent) : QObject(parent) {
    // ── 创建 ClientWorker（运行在独立线程）─────────────────
    client_       = new ClientWorker();
    clientThread_ = new QThread;
    client_->moveToThread(clientThread_);

    connect(client_, &ClientWorker::remoteJoined,  this, &Controller::onRemoteJoined,  Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteLeft,    this, &Controller::onRemoteLeft,    Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteIds,     this, &Controller::onRemoteIds,     Qt::QueuedConnection);
    connect(client_, &ClientWorker::pcStateChanged,this, &Controller::onPcStateChanged,Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteCall,    this, &Controller::onRemoteCall,    Qt::QueuedConnection);
    connect(client_, &ClientWorker::pcClosed,      this, &Controller::onPcClosed,      Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteMessage, this, &Controller::remoteMessage,   Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteVideoEnabled,
            this, &Controller::onRemoteVideoEnabled, Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteAudioEnabled,
            this, &Controller::onRemoteAudioEnabled, Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteGesture,
            this, &Controller::remoteGestureResult,  Qt::QueuedConnection);

    // 远端媒体帧（libwebrtc 解码后推来）
    connect(client_, &ClientWorker::remoteVideoFrame,
            this, &Controller::onRemoteVideoFrame, Qt::QueuedConnection);
    connect(client_, &ClientWorker::remoteAudioData,
            this, &Controller::onRemoteAudioData,  Qt::QueuedConnection);

    clientThread_->start();
    QMetaObject::invokeMethod(client_, "init", Qt::QueuedConnection);

    // ── 创建 MediaController（摄像头采集）─────────────────
    mediaController_ = new MediaController(this);
    connect(mediaController_, &MediaController::onLocalVideoFrame,
            this, &Controller::onLocalVideoFrame);
    connect(mediaController_, &MediaController::localGestureResult,
            this, &Controller::onLocalGestureResult);

    // 延迟到主 run loop 启动后再请求权限并初始化摄像头：
    // 1. app.exec() 尚未调用时 AVFoundation 无法弹出权限对话框
    // 2. requestAVPermissions() 内部用 dispatch_semaphore 阻塞直到用户响应，
    //    需在 run loop 已启动的状态下调用（否则 UI 事件无法处理）
    // 3. 权限确认后立即 init/start 摄像头
    QTimer::singleShot(0, this, [this]() {
        requestAVPermissions();             // 等待用户点"允许"（已授权则立即返回）
        mediaController_->startCaptureVideo();
    });
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
    if (swsCtx_) sws_freeContext(swsCtx_);
}

// ── 属性 ─────────────────────────────────────────────────

QString Controller::localId() const { return client_->getLocalId(); }

bool Controller::videoEnabled()      const { return videoEnabled_; }
bool Controller::remoteVideoEnabled()const { return remoteVideoEnabled_; }
bool Controller::audioEnabled()      const { return audioEnabled_; }
bool Controller::gestureEnabled()    const { return gestureEnabled_; }
bool Controller::bgEnabled()         const { return bgEnabled_; }

void Controller::setVideoEnabled(bool enabled) {
    if (enabled == videoEnabled_) return;
    videoEnabled_ = enabled;
    emit videoEnabledChanged(videoEnabled_);
    if (enabled) {
        mediaController_->startCaptureVideo(
            [this](AVFrame *frame) {
                // 用 AVFramePtr 捕获：若 clientThread_ 停止导致 lambda 被丢弃，
                // unique_ptr 析构会释放帧；正常执行时 release() 把所有权
                // 转回 pushVideoFrame → WebRTCVideoSource::pushFrame 负责释放。
                AVFramePtr owned(frame);
                QMetaObject::invokeMethod(
                    client_,
                    [this, owned = std::move(owned)]() mutable {
                        client_->pushVideoFrame(owned.release());
                    },
                    Qt::QueuedConnection);
            });
    } else {
        mediaController_->stopCaptureVideo();
    }
    client_->notifyVideoEnabled(enabled);
}

void Controller::setAudioEnabled(bool enabled) {
    if (enabled == audioEnabled_) return;
    audioEnabled_ = enabled;
    emit audioEnabledChanged(audioEnabled_);
    // libwebrtc ADM 控制麦克风，此处仅通知对端
    client_->notifyAudioEnabled(enabled);
}

void Controller::setGestureEnabled(bool enabled) {
    if (enabled == gestureEnabled_) return;
    gestureEnabled_ = enabled;
    emit gestureEnabledChanged(gestureEnabled_);
    if (enabled) mediaController_->startGesture();
    else         mediaController_->stopGesture();
}

void Controller::setBgEnabled(bool enabled) {
    if (enabled == bgEnabled_) return;
    bgEnabled_ = enabled;
    mediaController_->setBgEnabled(enabled);
    emit bgEnabledChanged(bgEnabled_);
}

// ── 槽 ───────────────────────────────────────────────────

void Controller::onRemoteJoined(QString id) { emit remoteJoined(id); }
void Controller::onRemoteLeft(QString id)   { emit remoteLeft(id); }
void Controller::onRemoteIds(QStringList ids) { emit remoteIds(ids); }

void Controller::onPcStateChanged(int state) {
    emit pcStateChanged(static_cast<PcState>(state));
    Logd("onPcStateChanged state={}", state);
    if (state == static_cast<int>(Connected)) {
        startMediaTransport();
    } else if (state == static_cast<int>(Closed)) {
        stopMediaTransport();
    }
}

void Controller::onRemoteCall(QString id) { emit remoteCall(id); }

void Controller::onPcClosed(QString id) {
    stopMediaTransport();
    emit pcClosed(id);
}

void Controller::startMediaTransport() {
    localVideoWidth_  = 0;
    localVideoHeight_ = 0;

    // 启动摄像头，同时推给 libwebrtc
    mediaController_->startCaptureVideo(
        [this](AVFrame *frame) {
            // webrtcCallback_ 在任意线程调用，直接调用（线程安全）
            client_->pushVideoFrame(frame);
        });
}

void Controller::stopMediaTransport() {
    Logd("stopMediaTransport");
    stopAsr();
    mediaController_->stopCaptureVideo();
}

void Controller::updateSwsContext(int width, int height, int format) {
    if (swsCtx_) sws_freeContext(swsCtx_);
    swsCtx_ = sws_getContext(
        width, height, static_cast<AVPixelFormat>(format),
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (yuvFrame_) { av_frame_unref(yuvFrame_); av_frame_free(&yuvFrame_); }
    yuvFrame_ = av_frame_alloc();
    yuvFrame_->format = AV_PIX_FMT_YUV420P;
    yuvFrame_->width  = width;
    yuvFrame_->height = height;
    if (av_frame_get_buffer(yuvFrame_, 0) < 0) {
        Loge("av_frame_get_buffer failed");
        av_frame_free(&yuvFrame_);
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
}

void Controller::callRemote(const QString &id) {
    QMetaObject::invokeMethod(client_, "call", Qt::QueuedConnection,
                              Q_ARG(QString, id));
}

void Controller::hungup() {
    stopMediaTransport();
    QMetaObject::invokeMethod(client_, "hungup", Qt::QueuedConnection);
}

void Controller::onRemoteVideoFrame(AVFrame *frame) {
    if (!frame) return;
    if (remoteVideoWidth_ != frame->width || remoteVideoHeight_ != frame->height) {
        remoteVideoWidth_  = frame->width;
        remoteVideoHeight_ = frame->height;
        Logd("remote video size: {}x{}", remoteVideoWidth_, remoteVideoHeight_);
        updateSwsContext(remoteVideoWidth_, remoteVideoHeight_, frame->format);
        emit remoteVideoSizeChanged(remoteVideoWidth_, remoteVideoHeight_);
    }

    YUVData yuvData;
    if (frame->format == AV_PIX_FMT_YUV420P) {
        extractYUVFromAVFrame(frame, yuvData);
    } else if (swsCtx_) {
        sws_scale(swsCtx_,
                  (const uint8_t *const *)frame->data, frame->linesize,
                  0, remoteVideoHeight_,
                  yuvFrame_->data, yuvFrame_->linesize);
        extractYUVFromAVFrame(yuvFrame_, yuvData);
    }
    emit receiveRemoteYuvData(yuvData);
    av_frame_unref(frame);
    av_frame_free(&frame);
}

void Controller::onLocalVideoFrame(AVFrame *frame) {
    if (!frame) return;
    if (localVideoWidth_ != frame->width || localVideoHeight_ != frame->height) {
        localVideoWidth_  = frame->width;
        localVideoHeight_ = frame->height;
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

void Controller::onRemoteAudioData(const void *data, int bits, int rate,
                                    size_t channels, size_t frames) {
    // 仅用于 ASR（语音识别）
    // libwebrtc ADM 已负责音频播放，此处只做 ASR 推送
    if (!asrClient_) return;

    // 将裸 PCM 包装成 AVFrame 送给 AsrClient（其内部会做重采样）
    AVFrame *avf = av_frame_alloc();
    avf->format      = AV_SAMPLE_FMT_S16;
    avf->sample_rate = rate;
    avf->nb_samples  = static_cast<int>(frames);
    av_channel_layout_default(&avf->ch_layout,
                               static_cast<int>(channels));

    // 分配缓冲并复制
    if (av_frame_get_buffer(avf, 0) >= 0) {
        size_t byteCount = frames * channels * (bits / 8);
        memcpy(avf->data[0], data, byteCount);
        asrClient_->pushAudioFrame(avf);
    }
    av_frame_unref(avf);
    av_frame_free(&avf);
}

void Controller::extractYUVFromAVFrame(AVFrame *frame, YUVData &yuv) {
    yuv.yLineSize = frame->linesize[0];
    yuv.uLineSize = frame->linesize[1];
    yuv.vLineSize = frame->linesize[2];
    yuv.height    = frame->height;

    int chromaH = frame->height / 2;

    // 每个平面在内存中连续（av_frame_get_buffer 保证），直接整块拷贝
    yuv.Y.resize(yuv.yLineSize * frame->height);
    memcpy(yuv.Y.data(), frame->data[0], yuv.yLineSize * frame->height);

    yuv.U.resize(yuv.uLineSize * chromaH);
    memcpy(yuv.U.data(), frame->data[1], yuv.uLineSize * chromaH);

    yuv.V.resize(yuv.vLineSize * chromaH);
    memcpy(yuv.V.data(), frame->data[2], yuv.vLineSize * chromaH);
}

void Controller::onRemoteVideoEnabled(bool enabled) {
    if (enabled == remoteVideoEnabled_) return;
    remoteVideoEnabled_ = enabled;
    emit remoteVideoEnabledChanged(remoteVideoEnabled_);
    // libwebrtc 内部管理 track，无需手动 start/stop receiver
}

void Controller::onRemoteAudioEnabled(bool enabled) {
    if (enabled == remoteAudioEnabled_) return;
    remoteAudioEnabled_ = enabled;
    emit remoteAudioEnabledChanged(remoteAudioEnabled_);
}

void Controller::onLocalGestureResult(Detection result) {
    int cx = result.box.x + result.box.width  / 2;
    int cy = result.box.y + result.box.height / 2;
    auto label = QString::fromStdString(result.label);
    QMetaObject::invokeMethod(client_, "sendGesture", Qt::QueuedConnection,
                              Q_ARG(int, cx), Q_ARG(int, cy),
                              Q_ARG(QString, label));
    emit localGestureResult(cx, cy, label);
}

void Controller::initVideoItem(QObject *mainWindow) {
    if (!mainWindow) { Loge("mainWindow is null"); return; }

    QObject *remoteVideoItem = mainWindow->findChild<QObject *>("remoteVideo");
    if (!remoteVideoItem) { Loge("remoteVideoItem is null"); return; }
    connect(this, SIGNAL(remoteVideoSizeChanged(int,int)),
            remoteVideoItem, SLOT(onVideoSizeChanged(int,int)),
            Qt::UniqueConnection);
    connect(this, SIGNAL(receiveRemoteYuvData(const YUVData &)),
            remoteVideoItem, SLOT(onReceiveVideoData(const YUVData &)),
            Qt::UniqueConnection);

    QObject *localVideoItem = mainWindow->findChild<QObject *>("localVideo");
    if (!localVideoItem) { Loge("localVideoItem is null"); return; }
    connect(this, SIGNAL(localVideoSizeChanged(int,int)),
            localVideoItem, SLOT(onVideoSizeChanged(int,int)),
            Qt::UniqueConnection);
    connect(this, SIGNAL(receiveLocalYuvData(const YUVData &)),
            localVideoItem, SLOT(onReceiveVideoData(const YUVData &)),
            Qt::UniqueConnection);
}

void Controller::sendMessage(QString message) {
    QMetaObject::invokeMethod(client_, "sendMessage", Qt::QueuedConnection,
                              Q_ARG(QString, message));
}

void Controller::startAsr() {
    asrClient_ = new AsrClient(this);
    connect(asrClient_, &AsrClient::asrText, this, &Controller::asrText);
}

void Controller::stopAsr() {
    if (asrClient_) {
        asrClient_->deleteLater();
        asrClient_ = nullptr;
    }
}
