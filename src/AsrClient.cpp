//
// Created by 余泓 on 2025/12/8.
//

#include "AsrClient.h"

#include "Logger.h"

#include <QJsonObject>
#include <QJsonArray>

#include "util.h"

AsrClient::AsrClient(QObject *parent)
    : QObject{parent} {
    ws_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    connect(ws_, &QWebSocket::connected, this, &AsrClient::onConnected);
    connect(ws_, &QWebSocket::disconnected, this, &AsrClient::onDisconnected);
    connect(ws_, &QWebSocket::textMessageReceived, this, &AsrClient::onTextMessage);

    connect(ws_, &QWebSocket::sslErrors, ws_, [=](const QList<QSslError> &errors){
        for (auto &e : errors) qDebug() << "SSL ERROR:" << e.errorString();
        ws_->ignoreSslErrors();
    });

    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    ssl.setProtocol(QSsl::TlsV1_2OrLater);
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);

    QNetworkRequest req(QUrl("wss://localhost:10095"));
    req.setSslConfiguration(ssl);

    req.setRawHeader("Sec-WebSocket-Protocol", "binary");

    ws_->open(req);
}

AsrClient::~AsrClient() {
    ws_->disconnect();
    ws_->close();
}

void AsrClient::pushAudioFrame(AVFrame *frame) {
    if (swrCtx_ == nullptr) {
        initSwr(frame);
    }

    if (swrCtx_ == nullptr) {
        Loge("swrCtx is null");
        return;
    }

    uint8_t *out_data[1];
    out_data[0] = reinterpret_cast<uint8_t *>(swrBuffer_.data());

    int converted = swr_convert(swrCtx_, out_data, MAX_OUT_SAMPLES, (const uint8_t **) frame->data, frame->nb_samples);
    if (converted < 0) {
        Loge("swr_convert error");
        return;
    }

    if (converted == 0)
        return;

    int outChannels = 1;
    int bytes = converted * outChannels * 2;
    sendAudioData(swrBuffer_.left(bytes));
}

void AsrClient::sendAudioData(const QByteArray &data) {
    if (!connected_) {
        return;
    }

    audioBuffer_.append(data);
    if (audioBuffer_.size() >= chunkSize_) {
        ws_->sendBinaryMessage(audioBuffer_);
        audioBuffer_.clear();
    }
}

void AsrClient::initSwr(AVFrame *frame) {
    swrCtx_ = swr_alloc();

    av_opt_set_chlayout(swrCtx_, "in_chlayout",  &frame->ch_layout, 0);
    av_opt_set_int      (swrCtx_, "in_sample_rate", frame->sample_rate, 0);
    av_opt_set_sample_fmt(swrCtx_, "in_sample_fmt", (AVSampleFormat)frame->format, 0);

    // 输出格式（转为单声道 + 16kHz + S16）
    AVChannelLayout outCh;
    av_channel_layout_from_mask(&outCh, AV_CH_LAYOUT_MONO);
    av_opt_set_chlayout(swrCtx_, "out_chlayout", &outCh, 0);
    av_opt_set_int      (swrCtx_, "out_sample_rate", 16000, 0);
    av_opt_set_sample_fmt(swrCtx_, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    int ret = swr_init(swrCtx_);
    if (ret < 0) {
        Loge("init swr_init failed: {}", av_errstr(ret));
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
    }

    swrBuffer_.resize(MAX_OUT_SAMPLES*2);
}

void AsrClient::onConnected() {
    Logd("connected");
    QJsonObject obj{
            {"mode", "2pass"},
            {"chunk_size", QJsonArray{5, 10, 5}},
            {"chunk_interval", 10},
            {"encoder_chunk_look_back", 4},
            {"decoder_chunk_look_back", 0},
            {"wav_name", "microphone"},
            {"is_speaking", true},
            {"hotwords", ""},
            {"itn", true}
    };
    QString json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    ws_->sendTextMessage(json);
    chunkSize_ = 1920; // 60 * 10(chunk_size[1]) * 16000(sample_rate) / 1000 * 2(bytes_per_sample) / 10(interval)
    connected_ = true;
}

void AsrClient::onTextMessage(const QString &msg) {
    QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8());
    QJsonObject obj = doc.object();
    QString text = obj["text"].toString();
    QString mode = obj["mode"].toString();
    bool end = mode == "2pass-offline";
    if (end) {
        curText_ = "";
        emit asrText(text, end);
    } else {
        curText_ += text;
        emit asrText(curText_, end);
    }
}

void AsrClient::onDisconnected() {
    Loge("disconnected");
}

