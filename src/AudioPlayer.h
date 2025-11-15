#pragma once
#include <QAudioSink>
#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <QFile>

#include "videoitem.h"

class AudioPlayer : public QObject {
    Q_OBJECT
public:
    AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer();

    void pushAudio(const char *data, int size);

private slots:
    void onAudioTimer();

private:
    QAudioSink *audioSink_ = nullptr;
    QIODevice *ioDevice_   = nullptr;
    QTimer *timer_         = nullptr;

    QByteArray buffer_;     // 软件 RingBuffer
    QMutex mutex_;
    int64_t ts_;
};