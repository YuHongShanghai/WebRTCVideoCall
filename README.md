# WebRTCVideoCall

基于 WebRTC 的实时音视频通话系统，集成语音识别、手势检测和人像分割等 AI 能力。

仓库包含三个子程序：

```
WebRTCVideoCall/
├── WebRTCClient/       # Qt 音视频客户端（主程序）
├── WebRTCClientServer/ # WebSocket 信令服务器
└── FunASRServer/       # 语音识别服务（ASR）
```

---

## WebRTCClient — 音视频客户端

### 功能

- 基于 **Google 原生 libwebrtc** 实现 WebRTC P2P 音视频通话（DTLS-SRTP、ICE、STUN）
- 使用 FFmpeg（avfoundation）采集摄像头并解码为 YUV420P；麦克风采集及音视频编解码由 libwebrtc 内置 ADM 和编解码器负责
- 集成 libwebrtc 内置 APM 进行回声消除（AEC）、降噪（ANS）、增益控制（AGC）等音频处理
- 集成 ONNX Runtime + OpenCV 实现：
  - **手势识别**（GestureInfer）：实时识别手势并显示检测框与标签
  - **人像分割**（SegInfer）：将背景替换为自定义图片（虚拟背景）
- 集成 AsrClient，将本地麦克风音频实时推送至 FunASRServer 进行语音识别，并在界面显示识别结果
- QML 界面，支持呼叫、挂断、设备选择等操作

### 架构说明

WebRTCClient 直接链接 Google 原生 `libwebrtc.a`（ARM64 静态库）。由于 libwebrtc 由
chromium clang 18 编译，使用 `std::__Cr` ABI，而 Qt/Apple clang 使用 `std::__1` ABI，
两者二进制不兼容，因此采用以下隔离策略：

- **C-ABI 公共接口**：`WebRTCClient.h` 只暴露 `const char*`、函数指针、POD 类型，
  不包含任何 libwebrtc 头文件，使 Qt moc 和 Apple clang 可安全包含
- **Pimpl 隔离**：所有 libwebrtc 对象（PeerConnection、DataChannel、视频源/接收器、
  音频接收器等）封装在 `WebRTCClient::Impl` 中，仅在 `.cpp` 文件内可见
- **chromium clang 桥接编译**：直接调用 libwebrtc API 的文件
  （`WebRTCClient.cpp`、`WebRTCVideoSource.cpp`、`AudioProcesser.cpp`）
  由 chromium clang 18 编译，生成 `std::__Cr` ABI 的目标文件，并预置于
  `third-party/libwebrtc/prebuilt/`；其余文件由 Apple clang 正常编译

### 依赖

| 依赖 | 说明 |
|------|------|
| Qt 6.x | Core / Gui / Qml / Quick / Multimedia / WebSockets |
| libwebrtc | 仓库内预编译（`third-party/libwebrtc/`），无需单独安装 |
| FFmpeg 7.x | 音视频采集与编解码 |
| OpenCV 4.x | 图像处理（手势/分割） |
| ONNX Runtime 1.23.2 | AI 推理（ARM64，已预置于 `third-party/`） |
| spdlog / nlohmann_json | 日志 / JSON |
| libomp（macOS） | OpenMP，用于 AI 推理加速 |

> `third-party/libwebrtc/` 内已包含：libwebrtc 所需全部头文件、静态库（`libwebrtc.a`
> 及视频编解码器工厂库）、chromium clang 18 可执行文件及运行时资源、预编译桥接目标文件。
> 大型二进制文件通过 **git-lfs** 管理。

### 构建

**前置条件：**
- 安装 [git-lfs](https://git-lfs.com/)，并在克隆后执行 `git lfs pull` 以拉取大型二进制文件
- 安装 Qt 6、FFmpeg 7、OpenCV 4、spdlog、nlohmann_json、libomp

```bash
git lfs pull   # 拉取 libwebrtc.a、prebuilt/*.o 等 LFS 文件

cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug --target appWebRTCClient -j8
```

### 配置

编辑 `WebRTCClient/src/config.h`：

```cpp
#define STUN_SERVER "stun:stun.l.google.com:19302"  // STUN 服务器
#define WS_SERVER   "ws://127.0.0.1:8000"            // 信令服务器地址
```

---

## WebRTCClientServer — 信令服务器

### 功能

基于 Qt WebSocket 实现的轻量级 WebRTC 信令服务器，负责在两端之间转发 SDP Offer/Answer
和 ICE Candidate，完成 P2P 连接协商。默认监听 `127.0.0.1:8000`。

### 依赖

| 依赖 | 版本要求 |
|------|---------|
| Qt | 6.x（Core / Gui / Network / WebSockets）|

### 构建与运行

```bash
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug --target WebRTCClientServer -j8

./cmake-build-debug/WebRTCClientServer/WebRTCClientServer
# 输出：Listening on 127.0.0.1:8000
```

---

## FunASRServer — 语音识别服务

### 功能

基于 [FunASR](https://github.com/modelscope/FunASR) 的流式语音识别 WebSocket 服务，
支持实时/离线两种识别模式，集成 VAD（语音活动检测）和标点恢复。WebRTCClient 的
AsrClient 模块会将本地麦克风音频推送至此服务进行识别。

默认加载模型：
- ASR：`iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-pytorch`
- 在线 ASR：`iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-online`
- VAD：`iic/speech_fsmn_vad_zh-cn-16k-common-pytorch`
- 标点：`iic/punc_ct-transformer_zh-cn-common-vad_realtime-vocab272727`

首次运行会自动从 ModelScope 下载模型到 `~/.cache/modelscope/`。

### 依赖与运行

```bash
pip install -r FunASRServer/requirements_server.txt

# 使用 SSL（wss://），使用仓库内自签名证书（默认）
python3 ./FunASRServer/funasr_wss_server.py

# 不使用 SSL（ws://），适合本地调试
python3 ./FunASRServer/funasr_wss_server.py --certfile "" --keyfile ""
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host` | `0.0.0.0` | 监听地址 |
| `--port` | `10095` | 监听端口 |
| `--ngpu` | `1` | GPU 数量，`0` 为纯 CPU |
| `--device` | `cuda` | 推理设备（`cuda` / `cpu`）|
| `--certfile` | `ssl_key/server.crt` | SSL 证书路径 |
| `--keyfile` | `ssl_key/server.key` | SSL 私钥路径 |

---

## 整体启动顺序

```
1. 启动信令服务器    WebRTCClientServer
2. 启动语音识别服务  FunASRServer/funasr_wss_server.py
3. 启动客户端       appWebRTCClient（两端各启动一个）
4. 一端点击"呼叫"，选择对端 ID，建立连接
```

## SSL 证书

仓库根目录 `ssl_key/` 下已预置自签名证书（有效期 10 年），仅用于本地/局域网开发测试。
生产环境请替换为受信任 CA 签发的证书。
