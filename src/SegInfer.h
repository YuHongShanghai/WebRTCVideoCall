//
// Created by 余泓 on 2026/1/11.
//

// ref: https://github.com/hpc203/PP-HumanSeg-opencv-onnxrun

#pragma once

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

class SegInfer {
public:
    // modelPath: ONNX 模型路径
    explicit SegInfer();

    ~SegInfer();

    // inframe: 输入视频帧（任意常见像素格式，如 NV12/YUV420P/...）
    // outframe: 输出视频帧（需要已分配，宽高与 inframe 一致，format 建议与 inframe 相同）
    // 返回 true 表示成功处理
    bool infer(const AVFrame* inframe, AVFrame* outframe);
    bool setBgImgPath(const std::string& bgImgPath);

private:
    bool initOrt(const std::string& modelPath);
    void ensureSws(const AVFrame* frame);
    void ensureBuffers(int srcW, int srcH);

    // 预处理：RGB(HWC, uint8) -> CHW(float)
    void preprocessToCHW(const cv::Mat& rgb);

    // 后处理：从 ORT 输出生成 mask（0/1），并在 rgb 上做叠加
    void postprocessAndBlend(cv::Mat& rgb, const Ort::Value& outTensor, int srcW, int srcH);

    void ensureBgFull(int srcW, int srcH);

    // Model input size
    int inputWidth_{0};
    int inputHeight_{0};

    // Runtime params
    float confThreshold_{0.6f};
    bool normalizeMinus1To1_{true}; // true: (x/255 - 0.5)/0.5 -> [-1,1]; false: x/255 -> [0,1]

    // ONNX Runtime
    Ort::Env env_;
    Ort::Session session_{nullptr};
    Ort::MemoryInfo memoryInfo_;

    std::vector<std::string> inputNamesStr_;
    std::vector<std::string> outputNamesStr_;
    std::vector<const char*> inputNames_;
    std::vector<const char*> outputNames_;
    std::vector<std::vector<int64_t>> inputNodeDims_;
    std::vector<std::vector<int64_t>> outputNodeDims_;

    // swscale
    SwsContext* swsToRgb_{nullptr};    // inframe -> RGB24
    SwsContext* swsFromRgb_{nullptr};  // RGB24 -> outframe(fmt)

    // Reused buffers/mats
    std::vector<uint8_t> rgbBuf_;          // srcW*srcH*3
    cv::Mat rgbMat_;                        // wraps rgbBuf_
    cv::Mat resizedRgb_;                    // inputHeight x inputWidth, CV_8UC3

    std::vector<float> inputTensor_;        // 1*3*H*W
    std::vector<int64_t> inputShape_{1, 3, 0, 0};

    // mask buffers (float and uint8)
    std::vector<float> maskProbBuf_;        // inputHeight*inputWidth (prob/person score) for resizing
    cv::Mat maskProbSmall_;                 // inputHeight x inputWidth, CV_32F (wrap)
    cv::Mat maskProbFull_;                  // srcH x srcW, CV_32F

    // Background
    std::mutex bgMutex_;
    bool bgEnabled_{false};
    std::string bgPath_;

    cv::Mat bgRgbOrig_;   // 背景原图（RGB，任意尺寸）
    cv::Mat bgRgbFull_;   // 背景缓存（RGB，srcH x srcW）
    int bgFullW_{0};
    int bgFullH_{0};
};
