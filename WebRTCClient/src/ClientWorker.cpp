//
// Created by 余泓 on 2025/11/1.
//

#include "ClientWorker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ClientInfo.h"
#include "Logger.h"

#define TYPE_MESSAGE "message"
#define TYPE_GESTURE "gesture"
#define KEY_TYPE "type"
#define KEY_CONTENT "content"
#define KEY_X "x"
#define KEY_Y "y"
#define KEY_LABEL "label"

QString jObjToStr(const QJsonObject &obj, bool indented = false) {
    QJsonDocument doc(obj);

    QJsonDocument::JsonFormat format =
        indented ? QJsonDocument::Indented : QJsonDocument::Compact;

    return QString::fromUtf8(doc.toJson(format));
}

ClientWorker::ClientWorker(QObject *parent) : QObject(parent) {}

void ClientWorker::init() {
    client_ = std::make_unique<WebRTCClient>();
    client_->setRoomClientsCallback(std::bind(&ClientWorker::onRoomClientsCallback, this, std::placeholders::_1));
    client_->setPcStateCallback(std::bind(&ClientWorker::onPcStateCallback, this, std::placeholders::_1));
    client_->setRemoteCallCallback(std::bind(&ClientWorker::onRemoteCallCallback, this, std::placeholders::_1));
    client_->setRemoteDataCallback(std::bind(&ClientWorker::onRemoteDataCallback, this, std::placeholders::_1));
    client_->connectSignalServer();
}

void ClientWorker::call(QString id) { client_->call(id.toStdString()); }

void ClientWorker::hungup() { client_->hungup(); }

void ClientWorker::sendMessage(QString message) {
    QJsonObject obj;
    obj[KEY_TYPE] = TYPE_MESSAGE;
    obj[KEY_CONTENT] = message;
    client_->sendData(jObjToStr(obj).toStdString());
}

void ClientWorker::sendGesture(int x, int y, QString label) {
    QJsonObject obj;
    obj[KEY_TYPE] = TYPE_GESTURE;
    obj[KEY_X] = x;
    obj[KEY_Y] = y;
    obj[KEY_LABEL] = label;
    client_->sendData(jObjToStr(obj).toStdString());
}

void ClientWorker::onRoomClientsCallback(std::string data) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(data).toUtf8(), &error);

    if (error.error != QJsonParseError::NoError) {
        Loge("JSON parse error {}", error.errorString().toStdString());
        return;
    }

    QJsonObject json = doc.object();

    if (json.contains("remote_joined")) {
        auto id = json["remote_joined"].toString();
        emit remoteJoined(id);
    } else if (json.contains("remote_ids")) {
        auto ids = json["remote_ids"].toArray();
        QStringList list;
        for (auto id: ids) {
            list.append(id.toString());
        }
        if (!list.empty()) {
            ClientInfo::instance()->setRemoteId(list[0].toStdString());
        }
        emit remoteIds(list);
    } else if (json.contains("remote_left")) {
        client_->hungup(false);
        auto id = json["remote_left"].toString();
        emit remoteLeft(id);
    } else if (json.contains("pc_closed")) {
        client_->hungup(false);
        emit pcClosed(json["pc_closed"].toString());
    } else if (json.contains("enable_video")) {
        emit remoteVideoEnabled(json["enable_video"].toBool());
    } else if (json.contains("enable_audio")) {
        emit remoteAudioEnabled(json["enable_audio"].toBool());
    }
}

void ClientWorker::onPcStateCallback(rtc::PeerConnection::State state) {
    emit pcStateChanged(state);
}

void ClientWorker::onRemoteCallCallback(std::string id) { emit remoteCall(QString::fromStdString(id)); }

void ClientWorker::onRemoteDataCallback(std::string data) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(data).toUtf8(), &error);

    if (error.error != QJsonParseError::NoError) {
        Loge("JSON parse error {}", error.errorString().toStdString());
        return;
    }

    QJsonObject json = doc.object();
    auto type = json[KEY_TYPE].toString();
    if (type == TYPE_MESSAGE) {
        emit remoteMessage(json[KEY_CONTENT].toString());
    } else if (type == TYPE_GESTURE) {
        emit remoteGesture(
            json[KEY_X].toInt(),
            json[KEY_Y].toInt(),
            json[KEY_LABEL].toString());
    }
}

ClientWorker::~ClientWorker() {}

QString ClientWorker::getLocalId() {
    if (!client_.get()) {
        return QString::fromStdString("");
    }
    return QString::fromStdString(client_->localId());
}

int ClientWorker::getVideoSrcPort() {
    if (!client_.get()) {
        return -1;
    }
    return client_->videoSrcPort();
}

int ClientWorker::getVideoSinkPort() {
    if (!client_.get()) {
        return -1;
    }
    return client_->videoSinkPort();
}
int ClientWorker::getAudioSrcPort() {
    if (!client_.get()) {
        return -1;
    }
    return client_->audioSrcPort();
}

int ClientWorker::getAudioSinkPort() {
    if (!client_.get()) {
        return -1;
    }
    return client_->audioSinkPort();
}

void ClientWorker::notifyVideoEnabled(bool enable) {
    if (client_.get()) {
        QJsonObject j;
        j["enable_video"] = enable;
        client_->sendWsMessage(jObjToStr(j).toStdString());
    }
}

void ClientWorker::notifyAudioEnabled(bool enable) {
    if (client_.get()) {
        QJsonObject j;
        j["enable_audio"] = enable;
        client_->sendWsMessage(jObjToStr(j).toStdString());
    }
}
