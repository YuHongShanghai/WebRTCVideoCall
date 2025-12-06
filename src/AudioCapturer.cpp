//
// Created by 余泓 on 2025/11/9.
//

#include "AudioCapturer.h"
#include "Logger.h"
#include "util.h"

#include <QAudioFormat>

AudioCapturer::AudioCapturer() {}

AudioCapturer::~AudioCapturer() {
    stop();
}

int AudioCapturer::init(int rtpPort) {
    avdevice_register_all();

    const AVInputFormat* inputFmt = av_find_input_format("avfoundation");
    if (!inputFmt) {
        Loge("Cannot find avfoundation input format");
        return -1;
    }

    AVDictionary *options = nullptr;
    av_dict_set(&options, "sample_rate", "48000", 0);
    av_dict_set(&options, "channels", "2", 0);

    std::string micNum = ":1";
    if (avformat_open_input(&inFmtCtx_, micNum.c_str(), inputFmt, &options) < 0) {
        Loge("Cannot open mic");
        return -1;
    }

    if (avformat_find_stream_info(inFmtCtx_, nullptr) < 0) {
        Loge("Failed to get stream info");
        return -1;
    }

    audioStreamIndex_ = av_find_best_stream(inFmtCtx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioStreamIndex_ < 0) {
        Loge("Cannot find audio stream");
        return -1;
    }

    AVStream* inStream = inFmtCtx_->streams[audioStreamIndex_];
    AVCodecParameters* inCodecPar = inStream->codecpar;

    const AVCodec* decoder = avcodec_find_decoder(inCodecPar->codec_id);
    Logd("decoder name: {}", decoder->name);
    decCtx_ = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decCtx_, inCodecPar);
    if (avcodec_open2(decCtx_, decoder, nullptr) < 0) {
        Loge("failed to open decoder");
        return -1;
    }

    const AVCodec *encoder = avcodec_find_encoder_by_name("libopus");
    if (!encoder) {
        Loge("libopus encoder not found");
        return -1;
    }

    encCtx_ = avcodec_alloc_context3(encoder);
    av_channel_layout_from_mask(&encCtx_->ch_layout, AV_CH_LAYOUT_STEREO);
    encCtx_->sample_rate = 48000;
    encCtx_->sample_fmt = AV_SAMPLE_FMT_S16;
    encCtx_->bit_rate = 64000;
    encCtx_->time_base = (AVRational){1, encCtx_->sample_rate};

    if (avcodec_open2(encCtx_, encoder, nullptr) < 0) {
        Loge("Could not open encoder");
        return -1;
    }

    auto outputUrl = std::string("rtp://127.0.0.1:") + std::to_string(rtpPort);
    if (avformat_alloc_output_context2(&outFmtCtx_, nullptr, "rtp", outputUrl.c_str()) < 0) {
        Loge("Failed to create RTP context");
        return -1;
    }

    outStream_ = avformat_new_stream(outFmtCtx_, encoder);
    if (!outStream_) {
        Loge("Failed to allocate output stream");
        return -1;
    }

    avcodec_parameters_from_context(outStream_->codecpar, encCtx_);
    outStream_->time_base = encCtx_->time_base;
    outStream_->id = 111;

    if (!(outFmtCtx_->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outFmtCtx_->pb, outputUrl.c_str(), AVIO_FLAG_WRITE) < 0) {
            Loge("Failed to open output URL");
            return -1;
        }
    }

    if (avformat_write_header(outFmtCtx_, nullptr) < 0) {
        Loge("Failed to write header");
        return -1;
    }

    av_dump_format(outFmtCtx_, 0, outputUrl.c_str(), 1);

    swrCtx_ = swr_alloc();
    if (!swrCtx_) {
        Loge("Failed to allocate swr");
        return -1;
    }

    av_opt_set_chlayout(swrCtx_, "in_chlayout",  &decCtx_->ch_layout, 0);
    av_opt_set_int      (swrCtx_, "in_sample_rate", decCtx_->sample_rate, 0);
    av_opt_set_sample_fmt(swrCtx_, "in_sample_fmt", decCtx_->sample_fmt, 0);
    av_opt_set_chlayout(swrCtx_, "out_chlayout", &encCtx_->ch_layout, 0);
    av_opt_set_int      (swrCtx_, "out_sample_rate", encCtx_->sample_rate, 0);
    av_opt_set_sample_fmt(swrCtx_, "out_sample_fmt", encCtx_->sample_fmt, 0);

    int ret = swr_init(swrCtx_);
    if (ret < 0) {
        Loge("Failed to initialize swr: {}", av_errstr(ret));
        return -1;
    }

    accFrame = av_frame_alloc();
    accFrame->format = encCtx_->sample_fmt;
    av_channel_layout_copy(&accFrame->ch_layout, &encCtx_->ch_layout);
    accFrame->sample_rate = encCtx_->sample_rate;
    accFrame->nb_samples = OPUS_FRAME_SIZE;
    av_frame_get_buffer(accFrame, 0);

    Logd("init success");
    return 0;
}

void AudioCapturer::capture() {
    AVPacket* inPkt = av_packet_alloc();
    AVPacket* outPkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    Logi("Start capturing");

    // 用于重采样输出的临时 interleaved S16 缓冲区
    const int MAX_OUT_SAMPLES = 2048;
    static std::vector<int16_t> swr_buffer;
    swr_buffer.resize(MAX_OUT_SAMPLES * encCtx_->ch_layout.nb_channels);

    int acc_samples = 0;
    int64_t audio_pts = 0; // 以 sample 为单位的时间戳

    while (running_) {
        int ret = av_read_frame(inFmtCtx_, inPkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                av_usleep(10000); // 休眠10ms再试
                continue;
            } else if (ret == AVERROR_EOF) {
                Logi("End of camera stream");
                break;
            } else {
                Loge("av_read_frame failed: {}", av_errstr(ret));
                continue;
            }
        }

        if (inPkt->stream_index != audioStreamIndex_) {
            av_packet_unref(inPkt);
            continue;
        }

        ret = avcodec_send_packet(decCtx_, inPkt);
        if (ret < 0) {
            Loge("avcodec_send_packet failed: {}", av_errstr(ret));
            av_packet_unref(inPkt);
            continue;
        }
        av_packet_unref(inPkt);

        while (avcodec_receive_frame(decCtx_, frame) == 0) {
            uint8_t *out_data[1];
            out_data[0] = reinterpret_cast<uint8_t*>(swr_buffer.data());

            int outSamples = swr_convert(swrCtx_,
                                          out_data, MAX_OUT_SAMPLES,
                                          (const uint8_t**)frame->data, frame->nb_samples);
            if (outSamples < 0) {
                Loge("swr_convert failed: {}", av_errstr(outSamples));
                continue;
            }

            if (outSamples == 0) {
                Logd("swr_convert no oputput");
                continue;
            }


            // --- accumulate and encode OPUS_FRAME_SIZE ---
            int remain = outSamples;
            int offset = 0;
            int channels = encCtx_->ch_layout.nb_channels;

            while (remain > 0) {
                int copy = FFMIN(remain, OPUS_FRAME_SIZE - acc_samples);
                int16_t *dst = (int16_t*)accFrame->data[0] + acc_samples * channels;
                int16_t *src = swr_buffer.data() + offset * channels;
                memcpy(dst, src, copy * channels * sizeof(int16_t));
                acc_samples += copy;
                offset += copy;
                remain -= copy;

                if (acc_samples == OPUS_FRAME_SIZE) {
                    // 填满一帧，送给 Opus 编码器
                    if ((ret = avcodec_send_frame(encCtx_, accFrame)) < 0) {
                        Loge("send_frame error: {}", av_errstr(ret));
                        break;
                    }
                    AVPacket *opkt = av_packet_alloc();
                    while (avcodec_receive_packet(encCtx_, opkt) == 0) {
                        opkt->stream_index = outStream_->index;
                        opkt->pts = audio_pts;
                        opkt->dts = audio_pts;
                        audio_pts += OPUS_FRAME_SIZE;

                        ret = av_interleaved_write_frame(outFmtCtx_, opkt);
                        if (ret < 0) {
                            Loge("av_interleaved_write_frame failed: {}", av_errstr(ret));
                        }
                        av_packet_unref(opkt);
                    }
                    av_packet_free(&opkt);
                    acc_samples = 0;
                }
            }
        }
    }

    av_packet_free(&inPkt);
    av_packet_free(&outPkt);
    av_frame_free(&frame);
}

void AudioCapturer::start() {
    if (running_) {
        return;
    }
    running_ = true;
    captureThread_ = new std::thread(&AudioCapturer::capture, this);
}

void AudioCapturer::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (captureThread_ && captureThread_->joinable())
        captureThread_->join();
    captureThread_ = nullptr;

    av_write_trailer(outFmtCtx_);

    swr_free(&swrCtx_);
    avcodec_free_context(&decCtx_);
    avcodec_free_context(&encCtx_);
    avformat_close_input(&inFmtCtx_);

    Logi("AudioCapturer stopped.");
}
