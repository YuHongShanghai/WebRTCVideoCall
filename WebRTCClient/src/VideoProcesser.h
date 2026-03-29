//
// Created by 余泓 on 2026/1/2.
//

#ifndef VIDEOPROCERSSER_H
#define VIDEOPROCERSSER_H

#include "GestureInfer.h"
#include "SegInfer.h"

class VideoProcesser {
public:
    VideoProcesser();
    ~VideoProcesser();
    Detection gestureRecognition(AVFrame *frame);
    void segmentation(AVFrame *inFrame, AVFrame *outFrame);
    void enableGestureDetection(bool enable);
    void enableSegmentation(bool enable);

private:
    std::unique_ptr<GestureInfer> gestureInfer_;
    std::unique_ptr<SegInfer> segInfer_;
};



#endif //VIDEOPROCERSSER_H
