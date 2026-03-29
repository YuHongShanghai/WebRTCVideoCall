/**
 * Qt signaling server example for libdatachannel
 * Copyright (c) 2022 cheungxiongwei
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "SignalingServer.h"
#include <QtWebSockets>
#include <format>
#include <QJsonArray>

SignalingServer::SignalingServer(QObject *parent) : QObject(parent) {
	server = new QWebSocketServer("SignalingServer", QWebSocketServer::NonSecureMode, this);
	QObject::connect(server, &QWebSocketServer::newConnection, this,
	                 &SignalingServer::onNewConnection);
}

bool SignalingServer::listen(const QHostAddress &address, quint16 port) {
	return server->listen(address, port);
}

void SignalingServer::onNewConnection() {
	auto webSocket = server->nextPendingConnection();
	auto client_id = webSocket->requestUrl().path().split("/").at(1);
	qInfo() << QString::fromStdString(
	    std::format("Client {} connected", client_id.toUtf8().constData()));

    for (auto ws: clients.values()) {
        QJsonObject obj;
        obj["remote_joined"] = client_id;
        QJsonDocument doc(obj);
        QString jsonString = doc.toJson(QJsonDocument::Compact);
        ws->sendTextMessage(QString(jsonString));
    }

    if (clients.size() > 0) {
        QJsonObject obj;
        QJsonArray arr;
        for (const auto &value : clients.keys()) {
            arr.append(value);
        }
        obj["remote_ids"] = arr;
        QJsonDocument doc(obj);
        QString jsonString = doc.toJson(QJsonDocument::Compact);
        webSocket->sendTextMessage(QString(jsonString));
    }

	clients[client_id] = webSocket;

	webSocket->setObjectName(client_id);
	QObject::connect(webSocket, &QWebSocket::disconnected, this, &SignalingServer::onDisconnected);
	QObject::connect(webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
	                 this, &SignalingServer::onWebSocketError);
	QObject::connect(webSocket, &QWebSocket::binaryMessageReceived, this,
	                 &SignalingServer::onBinaryMessageReceived);
	QObject::connect(webSocket, &QWebSocket::textMessageReceived, this,
	                 &SignalingServer::onTextMessageReceived);
}

void SignalingServer::onDisconnected() {
	QWebSocket *webSocket = qobject_cast<QWebSocket *>(sender());
	clients.remove(webSocket->objectName());

    auto id = webSocket->objectName();
    qInfo() << id << "disconnected";
    for (auto ws: clients.values()) {
        qDebug() << "send remote_left";
        QJsonObject obj;
        obj["remote_left"] = id;
        QJsonDocument doc(obj);
        QString jsonString = doc.toJson(QJsonDocument::Compact);
        ws->sendTextMessage(jsonString);
    }
}

void SignalingServer::onWebSocketError(QAbstractSocket::SocketError error) {
	qDebug() << QString::fromStdString(std::format("Client {} recv error {}",
	                                               sender()->objectName().toUtf8().constData(),
	                                               QString::number(error).toUtf8().constData()));
}

void SignalingServer::onBinaryMessageReceived(const QByteArray &message) {
	qInfo() << QString::fromStdString(std::format(
	    "Client {} << {}", sender()->objectName().toUtf8().constData(), message.constData()));
}

// 信令交互逻辑
void SignalingServer::onTextMessageReceived(const QString &message) {
	QWebSocket *webSocket = qobject_cast<QWebSocket *>(sender());

	qInfo() << QString::fromStdString(std::format("Client {} recv {}",
	                                              webSocket->objectName().toUtf8().constData(),
	                                              message.toUtf8().constData()));

	auto JsonObject = QJsonDocument::fromJson(message.toUtf8()).object();
    if (JsonObject.contains("close_pc_id")) {
        auto id = JsonObject["close_pc_id"].toString();
        auto ws = clients[id];
        if (ws) {
            QJsonObject obj;
            obj["pc_closed"] = webSocket->objectName();
            QJsonDocument doc(obj);
            QString jsonString = doc.toJson(QJsonDocument::Compact);
            ws->sendTextMessage(jsonString);
        }
        return;
    }

    if (JsonObject.contains("enable_video") ||
        JsonObject.contains("enable_audio")) {
        for (auto ws: clients.values()) {
            if (ws != webSocket) {
                ws->sendTextMessage(message);
            }
        }
        return;
    }

	auto destination_id = JsonObject["id"].toString();
	auto destination_websocket = clients[destination_id];
	if (destination_websocket) {
		JsonObject["id"] = webSocket->objectName(); // 将id替换成发送者id, 做转发
		auto data = QJsonDocument(JsonObject).toJson(QJsonDocument::Compact);
		qInfo() << QString::fromStdString(
		    std::format("Client {} send {}", destination_id.toUtf8().constData(), data.constData()));
		destination_websocket->sendTextMessage(QString(data));
		destination_websocket->flush();
	} else {
		qInfo() << QString::fromStdString(
		    std::format("Client {} not found", destination_id.toUtf8().constData()));
	}
}
