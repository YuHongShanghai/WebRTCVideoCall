#pragma once

#include <QObject>
#include <QThread>

extern "C" {
#include <libswresample/swresample.h>
}

#include "AsrClient.h"
#include "ClientWorker.h"
#include "MediaController.h"
#include "i420render.h"

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString localId READ localId CONSTANT)
    Q_PROPERTY(bool videoEnabled    READ videoEnabled    WRITE setVideoEnabled    NOTIFY videoEnabledChanged)
    Q_PROPERTY(bool remoteVideoEnabled READ remoteVideoEnabled NOTIFY remoteVideoEnabledChanged)
    Q_PROPERTY(bool audioEnabled    READ audioEnabled    WRITE setAudioEnabled    NOTIFY audioEnabledChanged)
    Q_PROPERTY(bool gestureEnabled  READ gestureEnabled  WRITE setGestureEnabled  NOTIFY gestureEnabledChanged)
    Q_PROPERTY(bool bgEnabled       READ bgEnabled       WRITE setBgEnabled       NOTIFY bgEnabledChanged)

public:
    explicit Controller(QObject *parent = nullptr);
    ~Controller();

    QString localId() const;
    bool videoEnabled()       const;
    void setVideoEnabled(bool enabled);
    bool remoteVideoEnabled() const;
    bool audioEnabled()       const;
    void setAudioEnabled(bool enabled);
    bool gestureEnabled()     const;
    void setGestureEnabled(bool enabled);
    bool bgEnabled()          const;
    void setBgEnabled(bool enabled);

    Q_INVOKABLE void callRemote(const QString &id);
    Q_INVOKABLE void hungup();
    Q_INVOKABLE void initVideoItem(QObject *mainWindow);
    Q_INVOKABLE void sendMessage(QString message);
    Q_INVOKABLE void startAsr();
    Q_INVOKABLE void stopAsr();

    // PcState 与 webrtc::PeerConnectionInterface::PeerConnectionState 顺序一致
    enum PcState {
        New          = 0,
        Connecting   = 1,
        Connected    = 2,
        Disconnected = 3,
        Failed       = 4,
        Closed       = 5
    };
    Q_ENUM(PcState)

public slots:
    void onRemoteJoined(QString id);
    void onRemoteLeft(QString id);
    void onRemoteIds(QStringList ids);
    void onPcStateChanged(int state);
    void onRemoteCall(QString id);
    void onPcClosed(QString id);
    void onRemoteVideoFrame(AVFrame *frame);
    void onLocalVideoFrame(AVFrame *frame);
    void onRemoteAudioData(const void *data, int bits, int rate,
                           size_t channels, size_t frames);
    void onRemoteVideoEnabled(bool enabled);
    void onRemoteAudioEnabled(bool enabled);
    void onLocalGestureResult(Detection result);
    void extractYUVFromAVFrame(AVFrame *frame, YUVData &yuv);

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
    void videoEnabledChanged(bool enabled);
    void remoteVideoEnabledChanged(bool enabled);
    void audioEnabledChanged(bool enabled);
    void remoteAudioEnabledChanged(bool enabled);
    void asrText(const QString &text, bool end);
    void localGestureResult(int x, int y, QString label);
    void gestureEnabledChanged(bool enabled);
    void remoteGestureResult(int x, int y, QString label);
    void bgEnabledChanged(bool enabled);

private:
    void startMediaTransport();
    void stopMediaTransport();
    void updateSwsContext(int width, int height, int format);

    ClientWorker    *client_;
    QThread         *clientThread_;
    MediaController *mediaController_ = nullptr;
    int remoteVideoWidth_  = 0;
    int remoteVideoHeight_ = 0;
    int localVideoWidth_   = 0;
    int localVideoHeight_  = 0;

    SwsContext *swsCtx_   = nullptr;
    AVFrame    *yuvFrame_ = nullptr;

    bool videoEnabled_        = true;
    bool remoteVideoEnabled_  = true;
    bool audioEnabled_        = true;
    bool remoteAudioEnabled_  = true;
    bool gestureEnabled_      = false;
    bool bgEnabled_           = false;

    AsrClient *asrClient_ = nullptr;
};
