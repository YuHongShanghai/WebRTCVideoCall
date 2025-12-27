//
// Created by 余泓 on 2025/12/18.
//

#include "AudioProcesser.h"

#include "Logger.h"

static inline size_t ringSize(const std::vector<int16_t>& ring,
                              size_t r, size_t w) {
    return (w + ring.size() - r) % ring.size();
}

AudioProcesser::AudioProcesser(int sampleRate, int channels): sampleRate_(sampleRate), channels_(channels) {
    apm_ = webrtc::AudioProcessingBuilder().Create();

    webrtc::AudioProcessing::Config config;
    config.echo_canceller.enabled = true;
    config.echo_canceller.mobile_mode = false;
    config.gain_controller1.enabled = true;
    config.gain_controller1.mode = webrtc::AudioProcessing::Config::GainController1::kAdaptiveAnalog;

    config.gain_controller2.enabled = true;

    config.high_pass_filter.enabled = true;
    config.noise_suppression.enabled = true;

    apm_->ApplyConfig(config);

    streamConfig_.set_sample_rate_hz(sampleRate);
    streamConfig_.set_num_channels(channels);
}

AudioProcesser::~AudioProcesser() {
}

void AudioProcesser::process(int16_t *in, int16_t *out, int totalFrames) {
    const int framesPer10ms = sampleRate_ / 100;
    const int samplesPer10ms = framesPer10ms * channels_;

    int processedFrames = 0;

    while (processedFrames + framesPer10ms <= totalFrames) {
        int16_t* inFrame  = in  + processedFrames * channels_;
        int16_t* outFrame = out + processedFrames * channels_;

        // WebRTC APM 要求：严格 10ms
        int ret = apm_->ProcessStream(
            inFrame,
            streamConfig_,
            streamConfig_,
            outFrame
        );

        if (ret != webrtc::AudioProcessing::kNoError) {
            Logw("ret: {}", ret);
            memcpy(outFrame,
                   inFrame,
                   samplesPer10ms * sizeof(int16_t));
        }

        processedFrames += framesPer10ms;
    }

    // 处理尾帧（不足 10ms 的部分，直接拷贝）
    int remainingFrames = totalFrames - processedFrames;
    if (remainingFrames > 0) {
        memcpy(out + processedFrames * channels_,
               in  + processedFrames * channels_,
               remainingFrames * channels_ * sizeof(int16_t));
    }
}

