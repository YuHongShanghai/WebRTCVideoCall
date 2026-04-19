//
// Created by 余泓 on 2026/1/2.
//

#include "VideoProcesser.h"

#include <QFile>

VideoProcesser::VideoProcesser() {
}

VideoProcesser::~VideoProcesser() {}

Detection VideoProcesser::gestureRecognition(AVFrame *frame) {
    std::shared_ptr<GestureInfer> snap;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        snap = gestureInfer_;
    }
    return snap ? snap->infer(frame) : Detection();
}

void VideoProcesser::segmentation(AVFrame *inFrame, AVFrame *outFrame) {
    // 短锁快照：避免推理过程中被 enableSegmentation(false) 析构导致 UAF。
    std::shared_ptr<SegInfer> snap;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        snap = segInfer_;
    }

    // 仅当开启分割时才为 outFrame 分配独立缓冲（SegInfer 要求宽高一致且有 buf）。
    // 未开启或推理失败则退化为对 inFrame 的引用，零拷贝直通。
    if (snap) {
        outFrame->format = inFrame->format;
        outFrame->width  = inFrame->width;
        outFrame->height = inFrame->height;
        if (av_frame_get_buffer(outFrame, 32) == 0 &&
            snap->infer(inFrame, outFrame)) {
            return;
        }
        av_frame_unref(outFrame);
    }
    av_frame_ref(outFrame, inFrame);
}

void VideoProcesser::enableGestureDetection(bool enable) {
    std::shared_ptr<GestureInfer> old;  // 在锁外析构，避免长时间持锁
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (enable) {
            if (!gestureInfer_) gestureInfer_ = std::make_shared<GestureInfer>();
        } else {
            old = std::move(gestureInfer_);
        }
    }
}

void VideoProcesser::enableSegmentation(bool enable) {
    std::shared_ptr<SegInfer> old;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (enable) {
            if (!segInfer_) {
                segInfer_ = std::make_shared<SegInfer>();
                segInfer_->setBgImgPath(std::string(CMAKE_CURRENT_SOURCE_DIR) + "/resources/seg_bg.jpg");
            }
        } else {
            old = std::move(segInfer_);
        }
    }
    // old 在此析构（锁外）：即便采集线程仍持有快照，也会等其释放后才销毁。
}
