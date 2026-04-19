#include "GestureInfer.h"

#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>

#include <fstream>
#include "Logger.h"

GestureInfer::GestureInfer()
    : env_(ORT_LOGGING_LEVEL_WARNING, "gesture"),
      session_(nullptr),
      memoryInfo_(Ort::MemoryInfo::CreateCpu(
          OrtDeviceAllocator, OrtMemTypeCPU))
{
    std::string modelPath = std::string(CMAKE_CURRENT_SOURCE_DIR) + "/models/YOLOv10n_gestures.onnx";
    inputWidth_  = 640;
    inputHeight_ = 640;

    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(2);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    session_ = Ort::Session(env_, modelPath.c_str(), opts);

    Ort::AllocatorWithDefaultOptions allocator;

    inputNames_.emplace_back(
        session_.GetInputNameAllocated(0, allocator).get());
    outputNames_.emplace_back(
        session_.GetOutputNameAllocated(0, allocator).get());

    labels_ = {"grabbing",
        "grip",
        "holy",
        "point",
        "call",
        "three3",
        "timeout",
        "xsign",
        "hand_heart",
        "hand_heart2",
        "little_finger",
        "middle_finger",
        "take_picture",
        "dislike",
        "fist",
        "four",
        "like",
        "mute",
        "ok",
        "one",
        "palm",
        "peace",
        "peace_inverted",
        "rock",
        "stop",
        "stop_inverted",
        "three",
        "three2",
        "two_up",
        "two_up_inverted",
        "three_gun",
        "thumb_index",
        "thumb_index2",
        "no_gesture"};
}

Detection GestureInfer::infer(AVFrame *frame) {
    if (!frame || !frame->data[0]) {
        Loge("no data");
        return {};
    }

    // 分帧跳过：大部分帧直接返回空 Detection，不跑 sws/预处理/YOLO。
    // 单检测用的 last-detection 不做缓存——手势变化快，重复 emit 反而会刷屏。
    if ((frameCounter_++ % inferStride_) != 0) {
        return {};
    }

    /*
     * 1. AVFrame (YUV/NV12/...) -> RGB24
     */
    if (swsCtx_ == nullptr) {
        initSws(frame);
    }

    if (swsCtx_ == nullptr) {
        return {};
    }

    const int srcW = frame->width;
    const int srcH = frame->height;
    const size_t rgbSize = static_cast<size_t>(srcW) * srcH * 3;
    if (rgbBuf_.size() != rgbSize) {
        rgbBuf_.assign(rgbSize, 0);
        cachedSrcW_ = srcW;
        cachedSrcH_ = srcH;
    }

    uint8_t *dst_data[4] = {rgbBuf_.data(), nullptr, nullptr, nullptr};
    int dst_linesize[4] = {srcW * 3, 0, 0, 0};

    sws_scale(swsCtx_, frame->data, frame->linesize, 0, srcH, dst_data, dst_linesize);

    /*
     * 2. RGB -> cv::Mat
     */
    cv::Mat rgb(srcH, srcW, CV_8UC3, rgbBuf_.data());

    /*
     * 3. 预处理：resize → 同时做 HWC→CHW + 1/255 归一化（一次遍历完成）
     *    省掉 convertTo + split 两次全尺寸额外 pass。
     */
    cv::Mat resized(inputHeight_, inputWidth_, CV_8UC3);
    cv::resize(rgb, resized, cv::Size(inputWidth_, inputHeight_), 0, 0, cv::INTER_LINEAR);

    const size_t tensorSize = static_cast<size_t>(3) * inputWidth_ * inputHeight_;
    if (inputTensor_.size() != tensorSize) {
        inputTensor_.assign(tensorSize, 0.f);
    }

    {
        const int H = inputHeight_;
        const int W = inputWidth_;
        float* outC0 = inputTensor_.data() + 0 * H * W;
        float* outC1 = inputTensor_.data() + 1 * H * W;
        float* outC2 = inputTensor_.data() + 2 * H * W;
        const float inv255 = 1.0f / 255.0f;
#pragma omp parallel for
        for (int i = 0; i < H; ++i) {
            const uint8_t* p = resized.ptr<uint8_t>(i);
            const int rowOff = i * W;
            for (int j = 0; j < W; ++j) {
                outC0[rowOff + j] = p[j * 3 + 0] * inv255;
                outC1[rowOff + j] = p[j * 3 + 1] * inv255;
                outC2[rowOff + j] = p[j * 3 + 2] * inv255;
            }
        }
    }

    std::vector<int64_t> input_shape = {1, 3, inputHeight_, inputWidth_};

    Ort::Value input_tensor_ort = Ort::Value::CreateTensor<float>(
            memoryInfo_, inputTensor_.data(), inputTensor_.size(), input_shape.data(), input_shape.size());

    const char *input_names[] = {inputNames_[0].c_str()};
    const char *output_names[] = {outputNames_[0].c_str()};

    /*
     * 5. 推理
     */
    auto outputs = session_.Run(Ort::RunOptions{nullptr}, input_names, &input_tensor_ort, 1, output_names, 1);

    /*
     * 6. 解析 YOLOv10 输出
     *    shape = [1, N, 6]
     *    [x1, y1, x2, y2, score, class_id]
     */
    float *data = outputs[0].GetTensorMutableData<float>();
    auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();

    if (shape.size() != 3 || shape[2] != 6) {
        std::cerr << "Unexpected YOLOv10 output shape!" << std::endl;
        return {};
    }

    int num_boxes = static_cast<int>(shape[1]);

    Detection result;
    float maxSocre = 0.0f;

    for (int i = 0; i < num_boxes; ++i) {
        float *ptr = data + i * 6;

        float x1 = ptr[0];
        float y1 = ptr[1];
        float x2 = ptr[2];
        float y2 = ptr[3];
        float score = ptr[4];
        int cls = static_cast<int>(ptr[5]);

        if (score < 0.6)
            continue;

        int left = x1;
        int top = y1;
        int right = x2;
        int bottom = y2;

        int width = right - left;
        int height = bottom - top;

        if (width <= 0 || height <= 0)
            continue;

        if (cls < 0 || cls >= labels_.size()) {
            Logw("cls is out of range: {}", cls);
            continue;
        }

        if (score > maxSocre) {
            maxSocre = score;
            result.box = cv::Rect(left, top, width, height);
            result.label = labels_[cls];
        }
    }

    return result;
}

GestureInfer::~GestureInfer() {
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
    }
}

void GestureInfer::initSws(AVFrame *frame) {
    swsCtx_ = sws_getCachedContext(nullptr, frame->width, frame->height,
                                           static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
                                           AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!swsCtx_) {
        Loge("Failed to create SwsContext");
    }
}
