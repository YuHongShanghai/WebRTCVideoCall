//
// Created by 余泓 on 2025/12/8.
//

#ifndef ASRCLIENT_H
#define ASRCLIENT_H

#include <QObject>
#include <QWebSocket>
#include <QByteArray>
#include <QFile>

extern "C" {
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

class AsrClient : public QObject
{
    Q_OBJECT
public:
    explicit AsrClient(QObject *parent = nullptr);
    ~AsrClient();
    void pushAudioFrame(AVFrame *frame);

signals:
    void asrText(const QString &text, bool end);

private slots:
    void onConnected();
    void onTextMessage(const QString &msg);
    void onDisconnected();

private:
    void sendAudioData(const QByteArray &data);
    void initSwr(AVFrame *frame);

    QWebSocket *ws_;
    bool connected_;
    SwrContext *swrCtx_ = nullptr;
    QByteArray swrBuffer_;
    QByteArray audioBuffer_;
    const int MAX_OUT_SAMPLES = 4096;
    int chunkSize_;

    QString curText_;
};


#endif //ASRCLIENT_H
