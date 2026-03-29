//
// Created by 余泓 on 2025/12/18.
//

#ifndef AUDIOPROCESSER_H
#define AUDIOPROCESSER_H

#include "api/scoped_refptr.h"
#include "modules/audio_processing/include/audio_processing.h"

class AudioProcesser {
public:
    AudioProcesser(int sampleRate, int channels);
    ~AudioProcesser();
    void process(int16_t *in, int16_t *out, int totalFrames);

private:
    rtc::scoped_refptr<webrtc::AudioProcessing> apm_;
    webrtc::StreamConfig streamConfig_;
    int sampleRate_;
    int channels_;
};



#endif //AUDIOPROCESSER_H
