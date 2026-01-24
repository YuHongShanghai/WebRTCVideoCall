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

    /*
     * 1. AVFrame (YUV/NV12/...) -> RGB24
     */
    if (swsCtx_ == nullptr) {
        initSws(frame);
    }

    if (swsCtx_ == nullptr) {
        return {};
    }

    std::vector<uint8_t> rgb_buffer(frame->width * frame->height * 3);

    uint8_t *dst_data[4] = {rgb_buffer.data(), nullptr, nullptr, nullptr};
    int dst_linesize[4] = {frame->width * 3, 0, 0, 0};

    sws_scale(swsCtx_, frame->data, frame->linesize, 0, frame->height, dst_data, dst_linesize);
    
    /*
     * 2. RGB -> cv::Mat
     */
    cv::Mat rgb(frame->height, frame->width, CV_8UC3, rgb_buffer.data());

    /*
     * 3. 预处理：resize + float32 + 1/255
     *    注意：这里仍是 direct resize（非 letterbox）
     */
    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(inputWidth_, inputHeight_));
    resized.convertTo(resized, CV_32F, 1.0 / 255.0);

    /*
     * 4. HWC -> NCHW
     */
    std::vector<float> input_tensor(3 * inputWidth_ * inputHeight_);

    std::vector<cv::Mat> chw(3);
    for (int i = 0; i < 3; ++i) {
        chw[i] = cv::Mat(inputHeight_, inputWidth_, CV_32F, input_tensor.data() + i * inputWidth_ * inputHeight_);
    }
    cv::split(resized, chw);

    std::vector<int64_t> input_shape = {1, 3, inputHeight_, inputWidth_};

    Ort::Value input_tensor_ort = Ort::Value::CreateTensor<float>(
            memoryInfo_, input_tensor.data(), input_tensor.size(), input_shape.data(), input_shape.size());

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
