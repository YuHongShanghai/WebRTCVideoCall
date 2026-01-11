//
// Created by 余泓 on 2026/1/2.
//

#ifndef VIDEOPROCERSSER_H
#define VIDEOPROCERSSER_H

#include "GestureInfer.h"

class VideoProcesser {
public:
    VideoProcesser();
    ~VideoProcesser();
    Detection gestureRecognition(AVFrame *frame);
    void enableGestureDetection(bool enable);

private:
    std::unique_ptr<GestureInfer> yoloV10Infer_;
};



#endif //VIDEOPROCERSSER_H
