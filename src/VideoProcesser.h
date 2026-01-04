//
// Created by 余泓 on 2026/1/2.
//

#ifndef VIDEOPROCERSSER_H
#define VIDEOPROCERSSER_H

#include "YoloV10Infer.h"

class VideoProcesser {
public:
    VideoProcesser();
    ~VideoProcesser();
    Detection gestureRecognition(AVFrame *frame);
    void enableGestureDetection(bool enable);

private:
    std::unique_ptr<YoloV10Infer> yoloV10Infer_;
};



#endif //VIDEOPROCERSSER_H
