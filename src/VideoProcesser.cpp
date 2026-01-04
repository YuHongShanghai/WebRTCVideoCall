//
// Created by 余泓 on 2026/1/2.
//

#include "VideoProcesser.h"

VideoProcesser::VideoProcesser() {
}

VideoProcesser::~VideoProcesser() {}

Detection VideoProcesser::gestureRecognition(AVFrame *frame) {
    return yoloV10Infer_ ? yoloV10Infer_->infer(frame) : Detection();
}

void VideoProcesser::enableGestureDetection(bool enable) {
    if (enable) {
        if (yoloV10Infer_ == nullptr) {
            std::string modelPath = std::string(CMAKE_CURRENT_SOURCE_DIR) + "/models/YOLOv10n_gestures.onnx";
            yoloV10Infer_ = std::make_unique<YoloV10Infer>(modelPath);
        }
    } else {
        yoloV10Infer_ = nullptr;
    }
}
