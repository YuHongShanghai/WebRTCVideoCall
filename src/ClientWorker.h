//
// Created by 余泓 on 2025/11/1.
//

#ifndef CLIENTWORKER_H
#define CLIENTWORKER_H

#include <QObject>
#include <QString>

#include "WebRTCClient.h"

class ClientWorker : public QObject {
Q_OBJECT

public:
    explicit ClientWorker(QObject *parent = nullptr);
    ~ClientWorker() override;
    QString getLocalId();
    int getVideoSrcPort();
    int getVideoSinkPort();
    int getAudioSrcPort();
    int getAudioSinkPort();
    void notifyVideoEnabled(bool enable);
    void notifyAudioEnabled(bool enable);

public slots:
    void init();
    void call(QString id);
    void hungup();
    void sendMessage(QString message);
    void sendGesture(int x, int y, QString label);

signals:
    void remoteJoined(QString id);
    void remoteLeft(QString id);
    void remoteIds(QStringList id);
    void pcStateChanged(rtc::PeerConnection::State state);
    void remoteCall(QString id);
    void pcClosed(QString id);
    void remoteMessage(QString message);
    void remoteVideoEnabled(bool enabled);
    void remoteAudioEnabled(bool enabled);
    void remoteGesture(int x, int y, QString label);

private:
    void onRoomClientsCallback(std::string data);
    void onPcStateCallback(rtc::PeerConnection::State state);
    void onRemoteCallCallback(std::string id);
    void onRemoteDataCallback(std::string data);

    std::unique_ptr<WebRTCClient> client_;
};


#endif //CLIENTWORKER_H
