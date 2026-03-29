//
// Created by 余泓 on 2025/11/8.
//

#ifndef MEDIACONTROLLER_H
#define MEDIACONTROLLER_H
#include <thread>
#include <QObject>

#include "AsrClient.h"
#include "AudioCapturer.h"
#include "AudioReceiver.h"
#include "GestureInfer.h"
#include "VideoCapturer.h"
#include "VideoReceiver.h"

class MediaController: public QObject {
    Q_OBJECT
public:
    MediaController(QObject *parent = nullptr);
    ~MediaController();

    void startCaptureVideo(int dstPort);
    void stopCaptureVideo();
    void startReceiveVideo(int port);
    void stopReceiveVideo();
    void recvRemoteVideoFrame(AVFrame *frame);
    void recvLocalVideoFrame(AVFrame *frame);
    void recvRemoteAudioFrame(AVFrame *frame);

    void startCaptureAudio(int dstPort);
    void stopCaptureAudio();
    void startReceiveAudio(int port);
    void stopReceiveAudio();

    void startGesture();
    void stopGesture();
    void setBgEnabled(bool enabled);

signals:
    void onRemoteVideoFrame(AVFrame *frame);
    void onLocalVideoFrame(AVFrame *frame);
    void onRemoteAudioFrame(AVFrame *frame);
    void localGestureResult(Detection result);

private:
    std::unique_ptr<VideoCapturer> videoCapturer_;
    std::unique_ptr<VideoReceiver> videoReceiver_;
    std::unique_ptr<AudioCapturer> audioCapturer_;
    std::unique_ptr<AudioReceiver> audioReceiver_;
};



#endif //MEDIACONTROLLER_H
