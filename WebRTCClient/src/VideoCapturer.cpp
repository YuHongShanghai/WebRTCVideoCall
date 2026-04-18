// 摄像头采集：基于 FFmpeg avfoundation/v4l2/dshow 输入。
// 内部捕获线程持续 av_read_frame，解码后将帧统一转换为 YUV420P，
// 再通过两个 C 函数指针回调（本地显示/AI、WebRTC 注入）分别交付。

#include "VideoCapturer.h"

#include <cstdlib>
#include <string>

#include "Logger.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

namespace {

#if defined(__APPLE__)
constexpr const char* kInputFormatName = "avfoundation";
constexpr const char* kDefaultDeviceUrl = "0";
#elif defined(__linux__)
constexpr const char* kInputFormatName = "v4l2";
constexpr const char* kDefaultDeviceUrl = "/dev/video0";
#elif defined(_WIN32)
constexpr const char* kInputFormatName = "dshow";
constexpr const char* kDefaultDeviceUrl = "video=Integrated Camera";
#else
constexpr const char* kInputFormatName = "v4l2";
constexpr const char* kDefaultDeviceUrl = "/dev/video0";
#endif

AVFrame* allocYuv420(int width, int height) {
    AVFrame* f = av_frame_alloc();
    f->format = AV_PIX_FMT_YUV420P;
    f->width  = width;
    f->height = height;
    if (av_frame_get_buffer(f, 32) < 0) {
        av_frame_free(&f);
        return nullptr;
    }
    return f;
}

} // namespace

VideoCapturer::VideoCapturer(void (*frameCallback)(AVFrame*, void*), void* ctx)
    : frameCallback_(frameCallback), frameCtx_(ctx) {}

VideoCapturer::~VideoCapturer() {
    stop();

    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    if (decCtx_) {
        avcodec_free_context(&decCtx_);
    }
    if (inFmtCtx_) {
        avformat_close_input(&inFmtCtx_);
    }
}

bool VideoCapturer::isRunning() const {
    return running_.load();
}

void VideoCapturer::setWebRTCCallback(void (*cb)(AVFrame*, void*), void* ctx) {
    webrtcCallback_ = cb;
    webrtcCtx_      = ctx;
}

int VideoCapturer::init() {
    if (inFmtCtx_) {
        Logi("VideoCapturer: already initialized");
        return 0;
    }

    avdevice_register_all();

    const AVInputFormat* inputFmt = av_find_input_format(kInputFormatName);
    if (!inputFmt) {
        Loge("VideoCapturer: input format '{}' not available", kInputFormatName);
        return -1;
    }

    // 设备 URL 选择：优先 WEBRTC_CAMERA_INDEX 环境变量，否则平台默认值
    const char* envIdx = std::getenv("WEBRTC_CAMERA_INDEX");
    std::string url;
#if defined(__APPLE__)
    url = (envIdx && *envIdx) ? envIdx : kDefaultDeviceUrl;
#elif defined(__linux__)
    url = (envIdx && *envIdx)
              ? (std::string("/dev/video") + envIdx)
              : kDefaultDeviceUrl;
#else
    url = kDefaultDeviceUrl;
#endif

    AVDictionary* options = nullptr;
    av_dict_set(&options, "framerate",    "30",        0);
    av_dict_set(&options, "video_size",   "1280x720",  0);
    av_dict_set(&options, "pixel_format", "nv12",      0);

    int ret = avformat_open_input(&inFmtCtx_, url.c_str(), inputFmt, &options);
    av_dict_free(&options);
    if (ret < 0) {
        Loge("VideoCapturer: cannot open camera '{}'", url);
        return -1;
    }
    Logi("VideoCapturer: camera opened, url={}", url);

    if (avformat_find_stream_info(inFmtCtx_, nullptr) < 0) {
        Loge("VideoCapturer: cannot find stream info");
        return -1;
    }

    videoStreamIndex_ =
        av_find_best_stream(inFmtCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex_ < 0) {
        Loge("VideoCapturer: cannot find video stream");
        return -1;
    }

    AVCodecParameters* par = inFmtCtx_->streams[videoStreamIndex_]->codecpar;
    const AVCodec*     dec = avcodec_find_decoder(par->codec_id);
    if (!dec) {
        Loge("VideoCapturer: decoder not found for codec_id={}", (int)par->codec_id);
        return -1;
    }

    decCtx_ = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(decCtx_, par);
    if (avcodec_open2(decCtx_, dec, nullptr) < 0) {
        Loge("VideoCapturer: failed to open decoder");
        return -1;
    }

    width_  = par->width;
    height_ = par->height;
    Logi("VideoCapturer: input {}x{} format={}",
         width_, height_,
         av_get_pix_fmt_name((AVPixelFormat)par->format));

    swsCtx_ = sws_getContext(width_, height_, (AVPixelFormat)par->format,
                             width_, height_, AV_PIX_FMT_YUV420P,
                             SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsCtx_) {
        Loge("VideoCapturer: sws_getContext failed");
        return -1;
    }

    Logi("VideoCapturer: initialized");
    return 0;
}

void VideoCapturer::captureLoop() {
    AVPacket* pkt      = av_packet_alloc();
    AVFrame*  decFrame = av_frame_alloc();

    Logi("VideoCapturer: capture loop started");

    while (running_.load()) {
        int ret = av_read_frame(inFmtCtx_, pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                av_usleep(10000);
                continue;
            }
            if (ret == AVERROR_EOF) {
                Logi("VideoCapturer: end of stream");
                break;
            }
            char err[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err, sizeof(err));
            Loge("VideoCapturer: av_read_frame failed: {}", err);
            break;
        }

        if (pkt->stream_index != videoStreamIndex_) {
            av_packet_unref(pkt);
            continue;
        }

        if (avcodec_send_packet(decCtx_, pkt) == 0) {
            while (avcodec_receive_frame(decCtx_, decFrame) == 0) {
                AVFrame* yuv = allocYuv420(width_, height_);
                if (!yuv) {
                    av_frame_unref(decFrame);
                    continue;
                }
                sws_scale(swsCtx_,
                          decFrame->data, decFrame->linesize, 0, height_,
                          yuv->data, yuv->linesize);
                yuv->pts = frameIndex_++;

                // ① 本地回调（显示 + AI）：传引用副本，原帧留给 ②
                if (frameCallback_) {
                    AVFrame* copy = av_frame_alloc();
                    av_frame_ref(copy, yuv);
                    frameCallback_(copy, frameCtx_);
                }

                // ② WebRTC 注入（所有权转给回调，由其负责释放）
                if (webrtcCallback_) {
                    webrtcCallback_(yuv, webrtcCtx_);
                } else {
                    av_frame_free(&yuv);
                }

                av_frame_unref(decFrame);
            }
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_frame_free(&decFrame);
    Logi("VideoCapturer: capture loop exited");
}

void VideoCapturer::start() {
    if (running_.load() || !inFmtCtx_) return;
    running_.store(true);
    captureThread_ = std::thread(&VideoCapturer::captureLoop, this);
    Logi("VideoCapturer: capture started");
}

void VideoCapturer::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    Logi("VideoCapturer: stopped");
}
