#ifndef ICESERVERCONFIG_H
#define ICESERVERCONFIG_H

#include <QJsonArray>
#include <QString>

class IceServerConfig {
public:
    IceServerConfig();

    QJsonArray toJson(const QString &clientId) const;
    bool hasTurn() const;

private:
    QString stunUrl_;
    QString turnHost_;
    QString turnRealm_;
    QString turnSecret_;
    QString turnUsername_;
    QString turnPassword_;
    int credentialTtlSeconds_ = 24 * 60 * 60;

    QString makeTurnUsername(const QString &clientId) const;
    QString makeTurnCredential(const QString &username) const;
};

#endif // ICESERVERCONFIG_H
