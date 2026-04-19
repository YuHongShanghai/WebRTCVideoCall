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

    // 复用缓冲：避免每帧在 heap 上 new/delete 几 MB
    std::vector<uint8_t> rgbBuf_;       // srcW*srcH*3
    std::vector<float>   inputTensor_;  // 1*3*inputH*inputW
    int cachedSrcW_ = 0;
    int cachedSrcH_ = 0;

    // 分帧跳过：YOLO 比分割更重，且手势变化慢，默认每 3 帧跑一次。
    // 跳过帧直接返回空 Detection（等价于「本帧未识别」），避免重复 emit。
    int frameCounter_ = 0;
    int inferStride_  = 3;
};

