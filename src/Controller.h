#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QObject>
#include <QThread>

extern "C" {
#include <libswresample/swresample.h>
}

#include "AudioPlayer.h"
#include "ClientWorker.h"
#include "MediaController.h"
#include "i420render.h"

class Controller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString localId READ localId CONSTANT)

public:
    Controller(QObject *parent = nullptr);
    ~Controller();
    QString localId() const;
    Q_INVOKABLE void callRemote(const QString &id);
    Q_INVOKABLE void hungup();
    Q_INVOKABLE void initVideoItem(QObject *mainWindow);
    Q_INVOKABLE void sendMessage(QString message);

    enum PcState {
        New = RTC_NEW,
        Connecting = RTC_CONNECTING,
        Connected = RTC_CONNECTED,
        Disconnected = RTC_DISCONNECTED,
        Failed = RTC_FAILED,
        Closed = RTC_CLOSED
    };

    Q_ENUM(PcState)

public slots:
    void onRemoteJoined(QString id);
    void onRemoteLeft(QString id);
    void onRemoteIds(QStringList ids);
    void onPcStateChanged(rtc::PeerConnection::State state);
    void onRemoteCall(QString id);
    void onPcClosed(QString id);
    void onRemoteVideoFrame(AVFrame *frame);
    void onLocalVideoFrame(AVFrame *frame);
    void extractYUVFromAVFrame(AVFrame* frame, YUVData &yuv);
    void onRemoteAudioFrame(AVFrame *frame);

signals:
    void remoteJoined(QString id);
    void remoteLeft(QString id);
    void remoteIds(QStringList id);
    void pcStateChanged(PcState);
    void remoteCall(QString id);
    void pcClosed(QString id);
    void remoteVideoSizeChanged(int width, int height);
    void receiveRemoteYuvData(const YUVData &yuv);
    void localVideoSizeChanged(int width, int height);
    void receiveLocalYuvData(const YUVData &yuv);
    void remoteMessage(QString message);

private:
    void startMediaTransport();
    void stopMediaTransport();
    void updateSwsContext(int width, int height, int format);
    void initSwrContext(AVFrame *frame);

    ClientWorker *client_;
    QThread *clientThread_;
    MediaController *mediaController_ = nullptr;
    int remoteVideoWidth_ = 0;
    int remoteVideoHeight_ = 0;
    int localVideoWidth_ = 0;
    int localVideoHeight_ = 0;

    SwsContext* swsCtx_ = nullptr;
    AVFrame *yuvFrame_ = nullptr;

    AudioPlayer *audioPlayer_;
    SwrContext *swrCtx_ = nullptr;
    std::vector<int16_t> swrBuffer_;
    const int MAX_OUT_SAMPLES = 4096;
};
#endif // MAINWINDOW_H
