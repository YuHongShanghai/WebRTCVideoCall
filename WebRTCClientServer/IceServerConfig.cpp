#include "IceServerConfig.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QProcessEnvironment>

static QString envValue(const char *name, const QString &fallback = {}) {
    const auto env = QProcessEnvironment::systemEnvironment();
    return env.value(QString::fromUtf8(name), fallback).trimmed();
}

static int envIntValue(const char *name, int fallback) {
    bool ok = false;
    const int value = envValue(name).toInt(&ok);
    return ok && value > 0 ? value : fallback;
}

IceServerConfig::IceServerConfig()
    : stunUrl_(envValue("WEBRTC_STUN_URL", "stun:stun.l.google.com:19302")),
      turnHost_(envValue("WEBRTC_TURN_HOST")),
      turnRealm_(envValue("WEBRTC_TURN_REALM", turnHost_)),
      turnSecret_(envValue("WEBRTC_TURN_SECRET")),
      turnUsername_(envValue("WEBRTC_TURN_USERNAME")),
      turnPassword_(envValue("WEBRTC_TURN_PASSWORD")),
      credentialTtlSeconds_(envIntValue("WEBRTC_TURN_TTL_SECONDS", 24 * 60 * 60)) {}

QJsonArray IceServerConfig::toJson(const QString &clientId) const {
    QJsonArray servers;

    if (!stunUrl_.isEmpty()) {
        QJsonObject stun;
        stun["urls"] = stunUrl_;
        servers.append(stun);
    }

    if (turnHost_.isEmpty()) {
        return servers;
    }

    const bool hasStaticCredential = !turnUsername_.isEmpty() && !turnPassword_.isEmpty();
    const bool hasDynamicCredential = !turnSecret_.isEmpty();
    if (!hasStaticCredential && !hasDynamicCredential) {
        return servers;
    }

    const QString username = hasStaticCredential ? turnUsername_ : makeTurnUsername(clientId);
    const QString credential = hasStaticCredential ? turnPassword_ : makeTurnCredential(username);

    QJsonArray urls;
    urls.append(QString("turn:%1:3478?transport=udp").arg(turnHost_));
    urls.append(QString("turn:%1:3478?transport=tcp").arg(turnHost_));
    if (envValue("WEBRTC_TURNS_ENABLED", "0") == "1") {
        urls.append(QString("turns:%1:5349?transport=tcp").arg(turnHost_));
    }

    QJsonObject turn;
    turn["urls"] = urls;
    turn["username"] = username;
    turn["credential"] = credential;
    if (!turnRealm_.isEmpty()) {
        turn["realm"] = turnRealm_;
    }
    servers.append(turn);

    return servers;
}

bool IceServerConfig::hasTurn() const {
    return !turnHost_.isEmpty()
           && (!turnSecret_.isEmpty() || (!turnUsername_.isEmpty() && !turnPassword_.isEmpty()));
}

QString IceServerConfig::makeTurnUsername(const QString &clientId) const {
    const qint64 expiry = QDateTime::currentSecsSinceEpoch() + credentialTtlSeconds_;
    return QString("%1:%2").arg(expiry).arg(clientId);
}

QString IceServerConfig::makeTurnCredential(const QString &username) const {
    const QByteArray hmac = QMessageAuthenticationCode::hash(
        username.toUtf8(),
        turnSecret_.toUtf8(),
        QCryptographicHash::Sha1);
    return QString::fromLatin1(hmac.toBase64());
}
