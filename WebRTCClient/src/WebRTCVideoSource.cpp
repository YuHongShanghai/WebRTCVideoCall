#include "WebRTCVideoSource.h"

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "Logger.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

WebRTCVideoSource::WebRTCVideoSource() = default;
WebRTCVideoSource::~WebRTCVideoSource() = default;

void WebRTCVideoSource::pushFrame(AVFrame *frame) {
    if (!frame) return;

    // 仅支持 YUV420P 输入（VideoCapturer 已保证此格式）
    if (frame->format != AV_PIX_FMT_YUV420P) {
        Loge("WebRTCVideoSource: expected YUV420P, got {}", frame->format);
        return;
    }

    int width  = frame->width;
    int height = frame->height;

    // 创建 I420Buffer 并从 AVFrame 复制数据
    rtc::scoped_refptr<webrtc::I420Buffer> buffer =
        webrtc::I420Buffer::Copy(
            width, height,
            frame->data[0], frame->linesize[0],   // Y
            frame->data[1], frame->linesize[1],   // U
            frame->data[2], frame->linesize[2]);  // V

    if (!buffer) {
        Loge("WebRTCVideoSource: I420Buffer::Copy failed");
        return;
    }

    // frame->pts 是帧序号（0,1,2,...），不是挂钟时间，不能作为时间戳。
    // 始终使用当前单调时钟，保证时间戳严格递增，防止编码器因
    // "Same/old NTP timestamp" 丢弃所有后续帧。
    webrtc::VideoFrame videoFrame =
        webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_timestamp_us(rtc::TimeMicros())
            .set_rotation(webrtc::kVideoRotation_0)
            .build();

    OnFrame(videoFrame);
}
