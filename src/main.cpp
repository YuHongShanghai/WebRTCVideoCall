#include <QGuiApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "Logger.h"
#include "Controller.h"
#include "videoitem.h"

int main(int argc, char *argv[])
{
    Logger::init("WebrtcClient");
    qputenv("QSG_RHI_BACKEND", "opengl");
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    Controller controller;
    engine.rootContext()->setContextProperty("controller", &controller);
    qmlRegisterUncreatableMetaObject(
        Controller::staticMetaObject,
        "WebrtcClient", 1, 0,
        "Controller",
        "Enums only"
    );

    qmlRegisterType<VideoItem>("VideoItem", 1, 0, "VideoItem");

    const QUrl url(QStringLiteral("qrc:/qt/qml/WebRTCClient/qml/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();

}
