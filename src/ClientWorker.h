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

public slots:
    void init();
    void call(QString id);
    void hungup();

signals:
    void remoteJoined(QString id);
    void remoteLeft(QString id);
    void remoteIds(QStringList id);
    void pcStateChanged(rtc::PeerConnection::State state);
    void remoteCall(QString id);
    void pcClosed(QString id);

private:
    void onRoomClientsCallback(std::string data);
    void onPcStateCallback(rtc::PeerConnection::State state);
    void onRemoteCallCallback(std::string id);

    std::unique_ptr<WebRTCClient> client_;
};


#endif //CLIENTWORKER_H
