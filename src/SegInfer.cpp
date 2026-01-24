#include "SegInfer.h"

#include "Logger.h"

#include <algorithm>
#include <cstring>

#include "util.h"

SegInfer::SegInfer()
    : env_(ORT_LOGGING_LEVEL_WARNING, "seg"),
      memoryInfo_(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU)) {
    std::string modelPath = std::string(CMAKE_CURRENT_SOURCE_DIR) + "/models/PPSeg.onnx";
    if (!initOrt(modelPath)) {
        Loge("SegInfer: initOrt failed");
    }
}

SegInfer::~SegInfer() {
    if (swsToRgb_) {
        sws_freeContext(swsToRgb_);
        swsToRgb_ = nullptr;
    }
    if (swsFromRgb_) {
        sws_freeContext(swsFromRgb_);
        swsFromRgb_ = nullptr;
    }
}

bool SegInfer::initOrt(const std::string& modelPath) {
    Ort::SessionOptions opts;
    // 实时场景一般倾向：线程不要太大，避免抢占解码/渲染线程
    opts.SetIntraOpNumThreads(2);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    try {
        session_ = Ort::Session(env_, modelPath.c_str(), opts);
    } catch (const Ort::Exception& e) {
        Loge("ORT Session create failed: {}", e.what());
        return false;
    }

    const size_t numInputNodes = session_.GetInputCount();
    const size_t numOutputNodes = session_.GetOutputCount();
    Ort::AllocatorWithDefaultOptions allocator;

    inputNamesStr_.clear();
    outputNamesStr_.clear();
    inputNodeDims_.clear();
    outputNodeDims_.clear();

    inputNamesStr_.reserve(numInputNodes);
    outputNamesStr_.reserve(numOutputNodes);

    for (size_t i = 0; i < numInputNodes; ++i) {
        auto nameAlloc = session_.GetInputNameAllocated(i, allocator);
        inputNamesStr_.emplace_back(nameAlloc.get());
        Ort::TypeInfo typeInfo = session_.GetInputTypeInfo(i);
        auto info = typeInfo.GetTensorTypeAndShapeInfo();
        auto dims = info.GetShape();
        inputNodeDims_.push_back(dims);
    }

    for (size_t i = 0; i < numOutputNodes; ++i) {
        auto nameAlloc = session_.GetOutputNameAllocated(i, allocator);
        outputNamesStr_.emplace_back(nameAlloc.get());
        auto typeInfo = session_.GetOutputTypeInfo(i);
        auto info = typeInfo.GetTensorTypeAndShapeInfo();
        auto dims = info.GetShape();
        outputNodeDims_.push_back(dims);
    }

    // cache const char*
    inputNames_.clear();
    outputNames_.clear();
    inputNames_.reserve(inputNamesStr_.size());
    outputNames_.reserve(outputNamesStr_.size());
    for (auto& s : inputNamesStr_) inputNames_.push_back(s.c_str());
    for (auto& s : outputNamesStr_) outputNames_.push_back(s.c_str());

    // 读取输入尺寸（假设 NCHW：1x3xHxW）
    if (inputNodeDims_.empty() || inputNodeDims_[0].size() < 4) {
        Loge("Unexpected input dims");
        return false;
    }

    const auto& inDims = inputNodeDims_[0];

    inputHeight_ = static_cast<int>(inDims[2]);
    inputWidth_  = static_cast<int>(inDims[3]);

    inputShape_ = {1, 3, inputHeight_, inputWidth_};

    Logi("SegInfer ORT init ok. Input={}x{}, inputs={}, outputs={}",
         inputWidth_, inputHeight_, inputNames_.size(), outputNames_.size());

    return true;
}

void SegInfer::ensureSws(const AVFrame* frame) {
    if (!frame) return;

    const int srcW = frame->width;
    const int srcH = frame->height;
    const auto srcFmt = static_cast<AVPixelFormat>(frame->format);

    // in -> RGB24
    swsToRgb_ = sws_getCachedContext(
        swsToRgb_,
        srcW, srcH, srcFmt,
        srcW, srcH, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!swsToRgb_) {
        Loge("Failed to create swsToRgb context");
        return;
    }
}

void SegInfer::ensureBuffers(int srcW, int srcH) {
    const size_t rgbSize = static_cast<size_t>(srcW) * static_cast<size_t>(srcH) * 3;

    if (rgbBuf_.size() != rgbSize) {
        rgbBuf_.assign(rgbSize, 0);
        rgbMat_ = cv::Mat(srcH, srcW, CV_8UC3, rgbBuf_.data());
    } else {
        // 尺寸不变时，rgbMat_ 仍有效；若外部 frame 尺寸变化，这里会重新 wrap
        rgbMat_ = cv::Mat(srcH, srcW, CV_8UC3, rgbBuf_.data());
    }

    if (resizedRgb_.empty() || resizedRgb_.rows != inputHeight_ || resizedRgb_.cols != inputWidth_) {
        resizedRgb_.create(inputHeight_, inputWidth_, CV_8UC3);
    }

    const size_t inTensorSize = static_cast<size_t>(3) * inputWidth_ * inputHeight_;
    if (inputTensor_.size() != inTensorSize) {
        inputTensor_.assign(inTensorSize, 0.f);
    }

    const size_t probSize = static_cast<size_t>(inputWidth_) * inputHeight_;
    if (maskProbBuf_.size() != probSize) {
        maskProbBuf_.assign(probSize, 0.f);
        maskProbSmall_ = cv::Mat(inputHeight_, inputWidth_, CV_32F, maskProbBuf_.data());
    } else {
        maskProbSmall_ = cv::Mat(inputHeight_, inputWidth_, CV_32F, maskProbBuf_.data());
    }

    if (maskProbFull_.empty() || maskProbFull_.rows != srcH || maskProbFull_.cols != srcW) {
        maskProbFull_.create(srcH, srcW, CV_32F);
    }
}

void SegInfer::preprocessToCHW(const cv::Mat& rgb) {
    // rgb: srcH x srcW, CV_8UC3, RGB order
    // resizedRgb_: inputH x inputW, CV_8UC3
    cv::resize(rgb, resizedRgb_, cv::Size(inputWidth_, inputHeight_), 0, 0, cv::INTER_LINEAR);

    // 直接填充 CHW float，避免 resized->float 再 split 的额外开销
    // inputTensor_ layout: [C][H][W]
    const int H = inputHeight_;
    const int W = inputWidth_;

    float* outC0 = inputTensor_.data() + 0 * H * W;
    float* outC1 = inputTensor_.data() + 1 * H * W;
    float* outC2 = inputTensor_.data() + 2 * H * W;

    // normalize
    // (x/255 - 0.5)/0.5  => x/127.5 - 1
    const float inv127_5 = 1.0f / 127.5f;

#pragma omp parallel for
    for (int i = 0; i < H; ++i) {
        const uint8_t* p = resizedRgb_.ptr<uint8_t>(i);
        const int rowOff = i * W;
        for (int j = 0; j < W; ++j) {
            // resizedRgb_ 是 RGB24
            const uint8_t r = p[j * 3 + 0];
            const uint8_t g = p[j * 3 + 1];
            const uint8_t b = p[j * 3 + 2];

            outC0[rowOff + j] = r * inv127_5 - 1.0f;
            outC1[rowOff + j] = g * inv127_5 - 1.0f;
            outC2[rowOff + j] = b * inv127_5 - 1.0f;
        }
    }
}

void SegInfer::postprocessAndBlend(cv::Mat& rgb, const Ort::Value& outTensor, int srcW, int srcH) {
    // 支持几种常见输出形状：
    // 1) [1,2,H,W] (NCHW two-class)
    // 2) [1,H,W,2] (NHWC two-class)
    // 3) [1,1,H,W] or [1,H,W] single channel
    // 4) [H,W] single channel

        auto info = outTensor.GetTensorTypeAndShapeInfo();
    const auto dims = info.GetShape();

    const float* ptr = outTensor.GetTensorData<float>();
    if (!ptr) {
        Loge("Null output tensor data");
        return;
    }

    // 解析输出为单通道人像 score/prob（默认取 class=1 为人像）
    int outH = 0, outW = 0;
    bool twoClass = false;
    bool nchw = false;

    if (dims.size() == 4) {
        if (dims[1] == 2) {              // 1x2xHxW
            twoClass = true; nchw = true;
            outH = (int)dims[2]; outW = (int)dims[3];
        } else if (dims[3] == 2) {       // 1xHxWx2
            twoClass = true; nchw = false;
            outH = (int)dims[1]; outW = (int)dims[2];
        } else if (dims[1] == 1) {       // 1x1xHxW
            twoClass = false;
            outH = (int)dims[2]; outW = (int)dims[3];
        } else {
            Loge("Unsupported output dims rank4");
            return;
        }
    } else if (dims.size() == 3) {       // 1xHxW or HxWx?
        if (dims[0] == 1) { outH = (int)dims[1]; outW = (int)dims[2]; }
        else { outH = (int)dims[0]; outW = (int)dims[1]; }
        twoClass = false;
    } else if (dims.size() == 2) {       // HxW
        outH = (int)dims[0]; outW = (int)dims[1];
        twoClass = false;
    } else {
        Loge("Unsupported output rank=%zu", dims.size());
        return;
    }

    if (outH <= 0 || outW <= 0) {
        Loge("Invalid output H/W");
        return;
    }

    // probSmall: 输出尺寸下的单通道 prob
    cv::Mat probSmall;
    std::vector<float> tmpProb;
    if (outH == inputHeight_ && outW == inputWidth_) {
        probSmall = maskProbSmall_;
    } else {
        tmpProb.assign((size_t)outH * outW, 0.f);
        probSmall = cv::Mat(outH, outW, CV_32F, tmpProb.data());
    }

    if (probSmall.data == nullptr) {
        return;
    }

    if (twoClass) {
        if (nchw) {
            const size_t plane = (size_t)outH * outW;
            const float* c1 = ptr + plane; // class=1
            for (int i = 0; i < outH; ++i) {
                float* dst = probSmall.ptr<float>(i);
                const size_t off = (size_t)i * outW;
                for (int j = 0; j < outW; ++j) dst[j] = c1[off + j];
            }
        } else {
            for (int i = 0; i < outH; ++i) {
                float* dst = probSmall.ptr<float>(i);
                const float* row = ptr + (size_t)i * outW * 2;
                for (int j = 0; j < outW; ++j) dst[j] = row[j * 2 + 1];
            }
        }
    } else {
        for (int i = 0; i < outH; ++i) {
            float* dst = probSmall.ptr<float>(i);
            const float* src = ptr + (size_t)i * outW;
            std::memcpy(dst, src, sizeof(float) * outW);
        }
    }

    // resize 到原图
    cv::resize(probSmall, maskProbFull_, cv::Size(srcW, srcH), 0, 0, cv::INTER_LINEAR);

    // 如果启用背景图：做换背景合成；否则保持 rgb 不变（你也可以在这里继续做其他叠加）
    bool doBg = false;
    {
        std::lock_guard<std::mutex> lk(bgMutex_);
        doBg = bgEnabled_ && !bgRgbOrig_.empty();
    }

    if (!doBg) {
        return;
    }

    ensureBgFull(srcW, srcH);

    // 合成：mask > threshold => 前景；否则 => 背景
    // rgb 与 bgRgbFull_ 均为 RGB8
    for (int y = 0; y < srcH; ++y) {
        const float* mp = maskProbFull_.ptr<float>(y);
        cv::Vec3b* fg = rgb.ptr<cv::Vec3b>(y);
        const cv::Vec3b* bg = bgRgbFull_.ptr<cv::Vec3b>(y);

        for (int x = 0; x < srcW; ++x) {
            if (mp[x] <= confThreshold_) {
                fg[x] = bg[x];
            }
        }
    }
}

bool SegInfer::infer(const AVFrame* inframe, AVFrame* outframe) {
    if (!inframe || !inframe->data[0]) {
        Loge("infer: inframe null or no data");
        return false;
    }
    if (!outframe) {
        Loge("infer: outframe null");
        return false;
    }
    if (inframe->width <= 0 || inframe->height <= 0) {
        Loge("infer: invalid inframe size");
        return false;
    }

    const int srcW = inframe->width;
    const int srcH = inframe->height;

    // 要求 outframe 的宽高一致（否则 sws 输出会错）
    if (outframe->width != srcW || outframe->height != srcH) {
        Loge("infer: outframe size mismatch. in={}x{} out={}x{}",
             srcW, srcH, outframe->width, outframe->height);
        return false;
    }

    // sws init + buffers
    ensureSws(inframe);
    if (!swsToRgb_) return false;
    ensureBuffers(srcW, srcH);

    // inframe -> RGB24
    uint8_t* dstData[4] = { rgbBuf_.data(), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { srcW * 3, 0, 0, 0 };

    sws_scale(
        swsToRgb_,
        inframe->data,
        inframe->linesize,
        0,
        srcH,
        dstData,
        dstLinesize
    );

    // rgbMat_ 是 RGB 顺序（由 sws 输出 RGB24）
    // 预处理生成 CHW
    preprocessToCHW(rgbMat_);

    // ORT input tensor
    Ort::Value inputTensorOrt = Ort::Value::CreateTensor<float>(
        memoryInfo_,
        inputTensor_.data(),
        inputTensor_.size(),
        inputShape_.data(),
        inputShape_.size()
    );

    // Run
    std::vector<Ort::Value> outputs;
    try {
        outputs = session_.Run(
            Ort::RunOptions{nullptr},
            inputNames_.data(),
            &inputTensorOrt,
            1,
            outputNames_.data(),
            outputNames_.size()
        );
    } catch (const Ort::Exception& e) {
        Loge("ORT Run failed: {}", e.what());
        return false;
    }

    if (outputs.empty()) {
        Loge("ORT output empty");
        return false;
    }

    // 后处理并叠加到 rgbMat_ 上（就地修改 rgbBuf_）
    postprocessAndBlend(rgbMat_, outputs[0], srcW, srcH);

    // RGB24 -> outframe format（通常希望与输入一致）
    const AVPixelFormat outFmt = static_cast<AVPixelFormat>(outframe->format);

    swsFromRgb_ = sws_getCachedContext(
        swsFromRgb_,
        srcW, srcH, AV_PIX_FMT_RGB24,
        srcW, srcH, outFmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsFromRgb_) {
        Loge("Failed to create swsFromRgb context");
        return false;
    }

    const uint8_t* srcData2[4] = { rgbBuf_.data(), nullptr, nullptr, nullptr };
    int srcLinesize2[4] = { srcW * 3, 0, 0, 0 };

    sws_scale(
        swsFromRgb_,
        srcData2,
        srcLinesize2,
        0,
        srcH,
        outframe->data,
        outframe->linesize
    );
    return true;
}

void SegInfer::ensureBgFull(int srcW, int srcH) {
    std::lock_guard<std::mutex> lk(bgMutex_);

    if (!bgEnabled_ || bgRgbOrig_.empty()) {
        return;
    }

    if (bgFullW_ == srcW && bgFullH_ == srcH && !bgRgbFull_.empty()) {
        return; // 缓存可复用
    }

    bgRgbFull_.create(srcH, srcW, CV_8UC3);
    cv::resize(bgRgbOrig_, bgRgbFull_, cv::Size(srcW, srcH), 0, 0, cv::INTER_LINEAR);
    bgFullW_ = srcW;
    bgFullH_ = srcH;
}

bool SegInfer::setBgImgPath(const std::string& bgImgPath) {
    std::lock_guard<std::mutex> lk(bgMutex_);

    if (bgImgPath.empty()) {
        bgEnabled_ = false;
        bgPath_.clear();
        bgRgbOrig_.release();
        bgRgbFull_.release();
        bgFullW_ = bgFullH_ = 0;
        Logi("Background disabled");
        return true;
    }

    // OpenCV 读入默认 BGR
    cv::Mat bgr = cv::imread(bgImgPath, cv::IMREAD_COLOR);
    if (bgr.empty()) {
        Loge("Failed to load background image: %s", bgImgPath.c_str());
        return false;
    }

    cv::cvtColor(bgr, bgRgbOrig_, cv::COLOR_BGR2RGB);

    bgEnabled_ = true;
    bgPath_ = bgImgPath;

    // 让缓存失效，下一帧按实际 srcW/srcH 重建
    bgRgbFull_.release();
    bgFullW_ = bgFullH_ = 0;

    Logi("Background loaded: {} ({}x{})", bgImgPath.c_str(), bgRgbOrig_.cols, bgRgbOrig_.rows);
    return true;
}

