#pragma once

#include "absl/types/optional.h"
#include "media/base/adapted_video_track_source.h"

extern "C" {
#include <libavutil/frame.h>
}

// 将 FFmpeg AVFrame (YUV420P) 转换为 webrtc::VideoFrame 并注入 libwebrtc 视频编码管线。
// 继承 rtc::AdaptedVideoTrackSource 以获得自适应、线程安全的帧分发能力。
// pushFrame() 可从任意线程调用。
class WebRTCVideoSource : public rtc::AdaptedVideoTrackSource {
public:
    WebRTCVideoSource();
    ~WebRTCVideoSource() override;

    // 推入一帧 YUV420P AVFrame，内部转换后注入 libwebrtc。
    // frame 的所有权不转移，调用方负责释放。
    void pushFrame(AVFrame *frame);

    // VideoTrackSourceInterface
    SourceState state() const override { return kLive; }
    bool remote() const override { return false; }
    bool is_screencast() const override { return false; }
    absl::optional<bool> needs_denoising() const override { return absl::nullopt; }
};
