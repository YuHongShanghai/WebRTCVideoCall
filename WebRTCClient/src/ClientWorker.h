#pragma once

#include <QObject>
#include <QWebSocket>
#include <QString>
#include <memory>

// Forward-declare to keep WebRTCClient.h (and transitively any libwebrtc/sigslot
// headers) away from Qt's moc, which redefines `emit` as an empty macro.
class WebRTCClient;

// AVFrame is a C struct; forward-declare so Qt moc doesn't need FFmpeg headers.
struct AVFrame;

// ClientWorker 运行在独立 QThread 上，持有 QWebSocket（信令）
// 和 WebRTCClient（libwebrtc P2P 层）。
//
// 信令：QWebSocket ←→ 信令服务器
// 媒体：WebRTCClient 通过回调通知 Controller
class ClientWorker : public QObject {
    Q_OBJECT

public:
    explicit ClientWorker(QObject *parent = nullptr);
    ~ClientWorker() override;

    QString getLocalId();

    // 通知远端摄像头/麦克风状态
    void notifyVideoEnabled(bool enable);
    void notifyAudioEnabled(bool enable);

    // 控制本地音频 track.enabled()（静音 / 取消静音）
    void setLocalAudioEnabled(bool enable);

    // WebSocket 实例：供 setWsSendCallback lambda 捕获
    QWebSocket *ws_ = nullptr;

public slots:
    void init();
    void call(QString id);
    void hungup();
    void sendMessage(QString message);
    void sendGesture(int x, int y, QString label);

    // 将本地视频帧推给 libwebrtc（可从任意线程调用）
    void pushVideoFrame(AVFrame *frame);

signals:
    void remoteJoined(QString id);
    void remoteLeft(QString id);
    void remoteIds(QStringList ids);
    void pcStateChanged(int state);        // 对应 Controller::PcState 枚举
    void remoteCall(QString id);
    void pcClosed(QString id);
    void remoteMessage(QString message);
    void remoteVideoEnabled(bool enabled);
    void remoteAudioEnabled(bool enabled);
    void remoteGesture(int x, int y, QString label);

    // 远端媒体帧（来自 libwebrtc 解码器）
    void remoteVideoFrame(AVFrame *frame);
    void remoteAudioData(const void *data, int bits, int rate,
                         size_t channels, size_t frames);

private slots:
    void onWsConnected();
    void onWsDisconnected();
    void onWsTextMessage(const QString &msg);

private:
    void onRoomClientsCallback(const char* data);
    void onPcStateCallback(int state);
    void onRemoteDataCallback(const char* data);

    std::unique_ptr<WebRTCClient> client_;
};
