#pragma once
extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}
#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

struct Detection {
    cv::Rect box;
    std::string label;
};

class GestureInfer {
public:
    GestureInfer();
    ~GestureInfer();
    Detection infer(AVFrame* frame);

private:
    void initSws(AVFrame *frame);

    Ort::Env env_;
    Ort::Session session_;
    Ort::MemoryInfo memoryInfo_;
    std::vector<std::string> inputNames_;
    std::vector<std::string> outputNames_;
    std::vector<std::string> labels_;
    int inputWidth_ = 640;
    int inputHeight_ = 640;
    SwsContext* swsCtx_ = nullptr;
};

