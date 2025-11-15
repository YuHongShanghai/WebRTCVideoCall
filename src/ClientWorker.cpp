//
// Created by 余泓 on 2025/11/1.
//

#include "ClientWorker.h"
#include <nlohmann/json.hpp>

#include "ClientInfo.h"

ClientWorker::ClientWorker(QObject *parent) : QObject(parent) {}

void ClientWorker::init() {
    client_ = std::make_unique<WebRTCClient>();
    client_->setRoomClientsCallback(std::bind(&ClientWorker::onRoomClientsCallback, this, std::placeholders::_1));
    client_->setPcStateCallback(std::bind(&ClientWorker::onPcStateCallback, this, std::placeholders::_1));
    client_->setRemoteCallCallback(std::bind(&ClientWorker::onRemoteCallCallback, this, std::placeholders::_1));
    client_->connectSignalServer();
}

void ClientWorker::call(QString id) { client_->call(id.toStdString()); }

void ClientWorker::hungup() {
    client_->hungup();
}

void ClientWorker::onRoomClientsCallback(std::string data) {
    nlohmann::json json = nlohmann::json::parse(data);
    if (json.contains("remote_joined")) {
        auto id = json["remote_joined"].get<std::string>();
        emit remoteJoined(QString::fromStdString(id));
    } else if (json.contains("remote_ids")) {
        auto ids = json["remote_ids"].get<std::vector<std::string>>();
        QStringList list;
        for (auto id: ids) {
            list.append(QString::fromStdString(id));
        }
        ClientInfo::instance()->setRemoteId(ids.at(0));
        emit remoteIds(list);
    } else if (json.contains("remote_left")) {
        client_->hungup(false);
        auto id = json["remote_left"].get<std::string>();
        emit remoteLeft(QString::fromStdString(id));
    } else if (json.contains("pc_closed")) {
        client_->hungup(false);
        emit pcClosed(QString::fromStdString(json["pc_closed"].get<std::string>()));
    }
}

void ClientWorker::onPcStateCallback(rtc::PeerConnection::State state) {
    emit pcStateChanged(state);
}

void ClientWorker::onRemoteCallCallback(std::string id) {
    emit remoteCall(QString::fromStdString(id));
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
