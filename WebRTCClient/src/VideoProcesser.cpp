//
// Created by 余泓 on 2026/1/2.
//

#include "VideoProcesser.h"

#include <QFile>

VideoProcesser::VideoProcesser() {
}

VideoProcesser::~VideoProcesser() {}

Detection VideoProcesser::gestureRecognition(AVFrame *frame) {
    return gestureInfer_ ? gestureInfer_->infer(frame) : Detection();
}

void VideoProcesser::segmentation(AVFrame *inFrame, AVFrame *outFrame) {
    if (segInfer_ == nullptr || !segInfer_->infer(inFrame, outFrame)) {
        av_frame_ref(outFrame, inFrame);
    }
}

void VideoProcesser::enableGestureDetection(bool enable) {
    if (enable) {
        if (gestureInfer_ == nullptr) {
            gestureInfer_ = std::make_unique<GestureInfer>();
        }
    } else {
        gestureInfer_ = nullptr;
    }
}

void VideoProcesser::enableSegmentation(bool enable) {
    if (enable) {
        if (segInfer_ == nullptr) {
            segInfer_ = std::make_unique<SegInfer>();
            segInfer_->setBgImgPath(std::string(CMAKE_CURRENT_SOURCE_DIR) + "/resources/seg_bg.jpg");
        }
    } else {
        segInfer_ = nullptr;
    }
}
