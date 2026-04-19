// WebRTCClient.h is now a pure C-ABI header — no libwebrtc includes inside.
// Safe to include anywhere without worrying about sigslot / ABI conflicts.
#include "ClientWorker.h"
#include "WebRTCClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QUrl>

#include "ClientInfo.h"
#include "Logger.h"
#include "config.h"

extern "C" {
#include <libavutil/frame.h>
}

// ClientInfo calls that WebRTCClient.cpp cannot make (ABI mismatch) are done here.
static void syncClientInfo(WebRTCClient* client) {
    if (!client) return;
    ClientInfo::instance()->setLocalId(std::string(client->localId()));
}

#define TYPE_MESSAGE "message"
#define TYPE_GESTURE "gesture"
#define KEY_TYPE    "type"
#define KEY_CONTENT "content"
#define KEY_X       "x"
#define KEY_Y       "y"
#define KEY_LABEL   "label"

static QString jObjToStr(const QJsonObject &obj) {
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ══════════════════════════════════════════════════════════

ClientWorker::ClientWorker(QObject *parent) : QObject(parent) {}

ClientWorker::~ClientWorker() {
    if (ws_) {
        ws_->close();
        ws_->deleteLater();
        ws_ = nullptr;
    }
}

void ClientWorker::init() {
    // ── 创建 WebRTCClient ──────────────────────────────────
    client_ = std::make_unique<WebRTCClient>();
    client_->init();
    syncClientInfo(client_.get());  // 同步 localId 到 ClientInfo（ABI 桥）

    // ── 设置 C 回调（不跨越 std::__1 / std::__Cr ABI 边界）────

    // 房间通知
    client_->setRoomClientsCallback(
        [](const char* data, void* ud) {
            static_cast<ClientWorker*>(ud)->onRoomClientsCallback(data);
        }, this);

    // PeerConnection 状态
    client_->setPcStateCallback(
        [](int state, void* ud) {
            static_cast<ClientWorker*>(ud)->onPcStateCallback(state);
        }, this);

    // 对端主动呼叫（作为 callee）
    client_->setRemoteCallCallback(
        [](const char* id, void* ud) {
            auto *self = static_cast<ClientWorker*>(ud);
            ClientInfo::instance()->setIsCaller(false);
            emit self->remoteCall(QString::fromUtf8(id));
        }, this);

    // DataChannel 消息
    client_->setRemoteDataCallback(
        [](const char* data, void* ud) {
            static_cast<ClientWorker*>(ud)->onRemoteDataCallback(data);
        }, this);

    // 远端视频帧（从 libwebrtc 解码线程调用，需 QueuedConnection）
    client_->setRemoteVideoCallback(
        [](AVFrame* frame, void* ud) {
            auto *self = static_cast<ClientWorker*>(ud);
            QMetaObject::invokeMethod(self, [self, frame]() {
                emit self->remoteVideoFrame(frame);
            }, Qt::QueuedConnection);
        }, this);

    // 远端音频数据（从 libwebrtc 音频线程调用）
    client_->setRemoteAudioCallback(
        [](const void* data, int bits, int rate,
           size_t channels, size_t frames, void* ud) {
            auto *self = static_cast<ClientWorker*>(ud);
            emit self->remoteAudioData(data, bits, rate, channels, frames);
        }, this);

    // ── 创建 QWebSocket（信令）────────────────────────────
    ws_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(ws_, &QWebSocket::connected,    this, &ClientWorker::onWsConnected);
    connect(ws_, &QWebSocket::disconnected, this, &ClientWorker::onWsDisconnected);
    connect(ws_, &QWebSocket::textMessageReceived,
            this, &ClientWorker::onWsTextMessage);

    // WebRTC → 信令服务器的发送通道
    client_->setWsSendCallback(
        [](const char* msg, void* ud) {
            auto *self = static_cast<ClientWorker*>(ud);
            QString qmsg = QString::fromUtf8(msg);
            QMetaObject::invokeMethod(self->ws_, [ws = self->ws_, qmsg]() {
                ws->sendTextMessage(qmsg);
            }, Qt::QueuedConnection);
        }, this);

    // 连接信令服务器
    QString url = QString(WS_SERVER) + "/" +
                  QString::fromUtf8(client_->localId());
    Logi("Connecting to signaling server: {}", url.toStdString());
    ws_->open(QUrl(url));
}

// ── WebSocket 槽 ──────────────────────────────────────────

void ClientWorker::onWsConnected() {
    Logi("WebSocket connected");
}

void ClientWorker::onWsDisconnected() {
    Logi("WebSocket disconnected");
}

void ClientWorker::onWsTextMessage(const QString &msg) {
    client_->onWsMessage(msg.toUtf8().constData());
}

// ── 呼叫控制 ──────────────────────────────────────────────

void ClientWorker::call(QString id) {
    ClientInfo::instance()->setIsCaller(true);
    client_->call(id.toUtf8().constData());
}
void ClientWorker::hungup() { client_->hungup(); }

void ClientWorker::pushVideoFrame(AVFrame *frame) {
    // 接管帧所有权：WebRTCVideoSource::pushFrame 已同步完成 I420Buffer::Copy，
    // 不再持有原 AVFrame，此处统一释放，避免整条链路的内存泄漏。
    if (client_) client_->pushVideoFrame(frame);
    av_frame_free(&frame);
}

void ClientWorker::sendMessage(QString message) {
    QJsonObject obj;
    obj[KEY_TYPE]    = TYPE_MESSAGE;
    obj[KEY_CONTENT] = message;
    client_->sendData(jObjToStr(obj).toUtf8().constData());
}

void ClientWorker::sendGesture(int x, int y, QString label) {
    QJsonObject obj;
    obj[KEY_TYPE]  = TYPE_GESTURE;
    obj[KEY_X]     = x;
    obj[KEY_Y]     = y;
    obj[KEY_LABEL] = label;
    client_->sendData(jObjToStr(obj).toUtf8().constData());
}

void ClientWorker::notifyVideoEnabled(bool enable) {
    if (!client_) return;
    QJsonObject j;
    j["enable_video"] = enable;
    client_->sendWsMessage(jObjToStr(j).toUtf8().constData());
}

void ClientWorker::notifyAudioEnabled(bool enable) {
    if (!client_) return;
    QJsonObject j;
    j["enable_audio"] = enable;
    client_->sendWsMessage(jObjToStr(j).toUtf8().constData());
}

QString ClientWorker::getLocalId() {
    if (!client_) return {};
    return QString::fromUtf8(client_->localId());
}

// ── 内部回调处理 ──────────────────────────────────────────

void ClientWorker::onRoomClientsCallback(const char* data) {
    QJsonParseError error;
    QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray(data), &error);
    if (error.error != QJsonParseError::NoError) {
        Loge("JSON parse error: {}", error.errorString().toStdString());
        return;
    }

    QJsonObject json = doc.object();

    if (json.contains("remote_joined")) {
        emit remoteJoined(json["remote_joined"].toString());

    } else if (json.contains("remote_ids")) {
        QStringList list;
        for (auto v : json["remote_ids"].toArray())
            list.append(v.toString());
        if (!list.empty())
            ClientInfo::instance()->setRemoteId(list[0].toStdString());
        emit remoteIds(list);

    } else if (json.contains("remote_left")) {
        client_->hungup(false);
        emit remoteLeft(json["remote_left"].toString());

    } else if (json.contains("pc_closed")) {
        client_->hungup(false);
        emit pcClosed(json["pc_closed"].toString());

    } else if (json.contains("enable_video")) {
        emit remoteVideoEnabled(json["enable_video"].toBool());

    } else if (json.contains("enable_audio")) {
        emit remoteAudioEnabled(json["enable_audio"].toBool());
    }
}

void ClientWorker::onPcStateCallback(int state) {
    emit pcStateChanged(state);
}

void ClientWorker::onRemoteDataCallback(const char* data) {
    QJsonParseError error;
    QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray(data), &error);
    if (error.error != QJsonParseError::NoError) {
        Loge("JSON parse error: {}", error.errorString().toStdString());
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
