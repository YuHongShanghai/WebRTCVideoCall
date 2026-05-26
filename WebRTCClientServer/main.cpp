
#include "SignalingServer.h"
#include <QCoreApplication>
#include <QDebug>
#include <QProcessEnvironment>

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString host = env.value("WEBRTC_SIGNAL_HOST", "0.0.0.0");
    const quint16 port = env.value("WEBRTC_SIGNAL_PORT", "8000").toUShort();

    SignalingServer server;
    if (server.listen(QHostAddress(host), port)) {
        qInfo() << QString("Listening on %1:%2").arg(host).arg(port);
        qInfo() << QString("server listening on %1:%2").arg(host).arg(port);
    } else {
        qDebug() << QString("[Errno 10000] error while attempting to bind on address ('%1', %2)")
                        .arg(host)
                        .arg(port);
    }
    return a.exec();
}
