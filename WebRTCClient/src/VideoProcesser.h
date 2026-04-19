//
// Created by 余泓 on 2026/1/2.
//

#ifndef VIDEOPROCERSSER_H
#define VIDEOPROCERSSER_H

#include <memory>
#include <mutex>

#include "GestureInfer.h"
#include "SegInfer.h"

// VideoProcesser 的 enable*/segmentation/gestureRecognition 会在不同线程调用：
//   - enable*()：UI 线程（来自 Controller::set...Enabled）
//   - segmentation/gesture*：采集线程（来自 MediaController::recvLocalVideoFrame）
// 使用 shared_ptr + 短锁快照：在采集线程短暂持锁拿到当前实例的副本，
// 之后在无锁状态下执行推理；UI 线程可随时安全地 reset，旧对象直到最后一个
// 快照释放才被销毁，避免推理过程中被析构导致 SIGSEGV。
class VideoProcesser {
public:
    VideoProcesser();
    ~VideoProcesser();
    Detection gestureRecognition(AVFrame *frame);
    void segmentation(AVFrame *inFrame, AVFrame *outFrame);
    void enableGestureDetection(bool enable);
    void enableSegmentation(bool enable);

private:
    std::shared_ptr<GestureInfer> gestureInfer_;
    std::shared_ptr<SegInfer>     segInfer_;
    std::mutex                    mutex_;  // 保护上面两个 shared_ptr 的替换
};



#endif //VIDEOPROCERSSER_H
