#include "AudioPlayer.h"
#include <QAudioFormat>
#include <QAudioSink>
#include <QMutexLocker>
#include <QTimer>

#include "Logger.h"
#include "util.h"

AudioPlayer::AudioPlayer(QObject *parent): QObject(parent)
{
    // 配置 PCM 格式
    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);

    audioSink_ = new QAudioSink(fmt, this);

    audioSink_->setBufferSize(48000);

    ioDevice_ = audioSink_->start();

    // 用 QTimer 模拟 Qt5 的 notify
    timer_ = new QTimer(this);
    timer_->setInterval(5);              // 每 5ms 写一次
    connect(timer_, &QTimer::timeout,
            this, &AudioPlayer::onAudioTimer);
    timer_->start();
}

AudioPlayer::~AudioPlayer()
{
    timer_->stop();
    audioSink_->stop();
}

void AudioPlayer::pushAudio(const char *data, int size)
{
    auto now = getTimeMillisecond();
    if (ts_ == 0) {
        ts_ = now;
    } else {
        auto interval = now - ts_;
        ts_ = now;
    }
    QMutexLocker lock(&mutex_);
    buffer_.append(data, size);

    // 避免环形 buffer 无限增长（例如超过 1 秒时丢掉旧数据）
    const int maxBuf = 48000 * 2; // 1 second buffer
    if (buffer_.size() > maxBuf) {
        buffer_.remove(0, buffer_.size() - maxBuf);
    }
}

void AudioPlayer::onAudioTimer()
{
    QMutexLocker lock(&mutex_);

    if (!ioDevice_)
        return;

    if (buffer_.isEmpty())
        return;

    qint64 freeBytes = audioSink_->bytesFree();
    if (freeBytes <= 0) {
        Loge("audio buffer is full");
        return;
    }

    qint64 toWrite = qMin(freeBytes, 960*2*2);
    qint64 written = ioDevice_->write(buffer_.data(), toWrite);

    if (written > 0) {
        buffer_.remove(0, written);
    }
}