# 技术架构文档

## 系统总览

```
┌─────────────────────────────────────────────────────────┐
│                     本地网络 / 局域网                      │
│                                                          │
│  ┌──────────────┐   WebSocket    ┌──────────────────┐    │
│  │ appWebRTC-   │◄──信令转发────►│ WebRTCClientServer│    │
│  │ Client (A)   │   :8000        │  SignalingServer  │    │
│  │              │                └──────────────────┘    │
│  │              │◄── P2P UDP ────────────────────────►   │
│  │              │   DTLS-SRTP                             │
│  └──────┬───────┘               ┌──────────────────┐    │
│         │ WebSocket (ASR)        │  FunASRServer    │    │
│         └───────────────────────►  :10095          │    │
│                                 └──────────────────┘    │
│                                                          │
│  ┌──────────────────────────────────────────────────┐   │
│  │ appWebRTCClient (B)                               │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

| 组件 | 语言/框架 | 说明 |
|------|-----------|------|
| `WebRTCClient` | C++20 / Qt 6 / QML | 主客户端，含 AI 推理 |
| `WebRTCClientServer` | C++20 / Qt 6 | WebSocket 信令服务器 |
| `FunASRServer` | Python / FunASR | 流式语音识别服务 |

---

## WebRTCClient

### 1. 整体分层

```
┌──────────────────────────────────────────────────────────────┐
│  QML 界面层                                                   │
│  Main.qml / VideoItem / GestureLayer / AsrPanel / …          │
└────────────────────────┬─────────────────────────────────────┘
                         │ Q_PROPERTY / Q_INVOKABLE / signals
┌────────────────────────▼─────────────────────────────────────┐
│  Controller（主线程 QObject）                                  │
│  • QML ↔ 业务逻辑的唯一桥接点                                  │
│  • 管理 ClientWorker 线程 / MediaController                    │
│  • 转发 YUV 帧给 VideoItem 渲染                               │
│  • 管理 AsrClient                                             │
└──────┬──────────────────────────────┬────────────────────────┘
       │                              │
┌──────▼──────────┐          ┌────────▼────────────────────────┐
│  ClientWorker   │          │  MediaController（主线程）        │
│  （独立 QThread）│          │  • 摄像头采集 (VideoCapturer)     │
│  • WebSocket    │          │  • AI 处理 (VideoProcesser)      │
│  • WebRTCClient │          │    ├─ GestureInfer（手势识别）    │
│    (libwebrtc)  │          │    └─ SegInfer（人像分割）        │
└─────────────────┘          └─────────────────────────────────┘
```

### 2. 关键模块

#### 2.1 Controller

主线程的总控节点，通过 Q_PROPERTY 和 signals/slots 与 QML 双向绑定。

- 以 `QThread` 管理 `ClientWorker` 的生命周期，保证 `QWebSocket` 和 libwebrtc 事件循环独立运行
- 从 `ClientWorker` 收到远端视频 `AVFrame*` 后，转换 YUV 数据发给 `VideoItem` 渲染
- 从 `MediaController` 收到本地视频帧，经同样路径渲染本地画面
- 管理 `AsrClient`：将远端音频 PCM 送入 ASR 流水线

#### 2.2 ClientWorker

运行在独立 `QThread`，持有两个核心对象：

| 对象 | 职责 |
|------|------|
| `QWebSocket` | 连接信令服务器，收发 JSON 信令消息 |
| `WebRTCClient` | 封装 libwebrtc P2P 会话，C-ABI 公共接口 |

初始化时为 `WebRTCClient` 注册 5 类 C 函数指针回调，通过 `QMetaObject::invokeMethod` 安全地从 libwebrtc 内部线程切回 Qt 事件循环：

```
libwebrtc 线程 → C 回调 → QMetaObject::invokeMethod(QueuedConnection)
                                         → ClientWorker Qt 槽
                                         → emit signal → Controller
```

#### 2.3 WebRTCClient（libwebrtc 封装）

**ABI 隔离架构**

libwebrtc 由 chromium clang 18 编译，使用 `std::__Cr` ABI；Qt/系统代码使用 Apple clang 的 `std::__1` ABI，两者二进制不兼容。所有越界 API（包括 `setLocalAudioEnabled` 等控制入口）一律只传 POD 参数，内部通过 Pimpl 转发到 `std::__Cr` 侧。三层隔离：

```
┌──────────────────────────────────────────────────────┐
│  公共头文件 WebRTCClient.h                             │
│  • 仅暴露 const char*, 函数指针, POD 类型              │
│  • 不包含任何 libwebrtc 头文件                         │
│  • Qt moc / Apple clang 可安全包含                    │
└──────────────────────────┬───────────────────────────┘
                           │ Pimpl
┌──────────────────────────▼───────────────────────────┐
│  WebRTCClient::Impl（仅在 .cpp 内可见）               │
│  • PeerConnectionFactory / PeerConnection             │
│  • DataChannel / VideoSource / VideoSink / AudioSink  │
│  • 3 个独立 rtc::Thread（network/worker/signaling）    │
└──────────────────────────┬───────────────────────────┘
                           │ chromium clang 编译
┌──────────────────────────▼───────────────────────────┐
│  prebuilt/WebRTCClient.cpp.o                          │
│  prebuilt/WebRTCVideoSource.cpp.o                     │
│  （std::__Cr ABI，链接 libwebrtc.a）                  │
└──────────────────────────────────────────────────────┘
```

**音频 3A 处理**

在 `addMediaTracks()` 中调用 `pcFactory_->CreateAudioSource(cricket::AudioOptions())`，`AudioOptions` 的 `echo_cancellation`/`auto_gain_control`/`noise_suppression` 默认全部为 `true`，由 libwebrtc voice engine 内部配置 APM 并应用于采集流。无需手动实例化 `AudioProcessing`。

**本地静音（Mute）**

Controller 的 `setAudioEnabled(false)` 会触发两件事：
1. 通过信令向对端广播 `{"enable_audio": false}`（用于 UI 提示）
2. 调用 `WebRTCClient::setLocalAudioEnabled(false)` → Pimpl 内遍历 `pc_->GetSenders()`，对 `kind() == kAudioKind` 的 track 执行 `AudioTrackInterface::set_enabled(false)`

`set_enabled(false)` 令 libwebrtc 持续发送 comfort noise / 静音包（而非断流），对端播放静音，ICE/RTP 状态保持稳定。新创建的 track 在 `addMediaTracks` 中也会按 `localAudioEnabled_` 状态同步。

**P2P 会话流程**

```
Caller                  SignalingServer              Callee
  │                           │                        │
  │── call(id) ──────────────►│                        │
  │   CreateOffer             │                        │
  │   SetLocalDesc            │                        │
  │── {type:"offer",sdp} ────►│──── {type:"offer"} ──►│
  │                           │     SetRemoteDesc      │
  │                           │     CreateAnswer       │
  │                           │     SetLocalDesc       │
  │◄─ {type:"answer",sdp} ───│◄─── {type:"answer"} ──│
  │   SetRemoteDesc           │                        │
  │◄──── ICE candidates ─────►│◄──── ICE candidates ──│
  │                   (P2P UDP 直连建立)                │
```

#### 2.4 MediaController

主线程对象，桥接摄像头采集与 AI 处理流水线。2026/1 起摄像头采集全面迁移到 FFmpeg `avdevice`，不再使用 AVFoundation Objective-C 直采（仅权限申请 `requestPermissions.mm` 仍为 .mm）。

```
FFmpeg avdevice 输入 (avfoundation / v4l2 / dshow)
      │ av_read_frame + avcodec_send/receive_packet
      │ swscale: 原生像素格式 → YUV420P
      ▼
VideoCapturer（std::thread 捕获循环）
      │ 单条 C 函数指针回调（frameCallback_）
      ▼
MediaController::localFrameTrampoline → recvLocalVideoFrame(AVFrame*)
      │
      ├─ VideoProcesser::gestureRecognition(frame)     原始帧，不修改
      │    └─ emit localGestureResult(Detection)
      │
      ├─ VideoProcesser::segmentation(frame, outFrame) 可能分配新缓冲或 av_frame_ref 直通
      │
      ├─ webrtcSink_(ref(outFrame))  ── 分叉给 WebRTC ─►
      │                                    ClientWorker::pushVideoFrame
      │                                    └─► WebRTCVideoSource::pushFrame
      │                                         （I420Buffer::Copy + rtc::TimeMicros）
      │                                         └─► libwebrtc 编码 → RTP → 网络
      │
      └─ emit onLocalVideoFrame(outFrame)  ── 本地预览分叉
```

要点：
- 原始帧 `frame` 由 `av_frame_free(&frame)` 释放（`recvLocalVideoFrame` 持有所有权）
- WebRTC 与预览各自拿到对 `outFrame` 的独立引用（`av_frame_ref`），后续各自释放
- 分割关闭时 `segmentation()` 走 `av_frame_ref(outFrame, inFrame)` 零拷贝直通；开启时先 `av_frame_get_buffer(outFrame, 32)` 再推理，分割失败回退直通
- 所有 WebRTC 帧（包括已替换背景的画面）都经同一条链路，保证远端看到与本地一致的输出

**VideoProcesser 线程安全**

`enable*()` 由 UI 线程调用，`gestureRecognition/segmentation` 由 VideoCapturer 的采集线程调用。两条线程通过 `shared_ptr<Infer> + std::mutex` 的「短锁快照」模式解耦：

```
采集线程                     UI 线程
  │                            │
  │  lock → snap = infer_      │
  │  unlock                    │
  │                            │  lock → old = move(infer_)
  │  snap->infer(...)          │  unlock
  │  （无锁热路径）              │  old 析构（若快照仍在用则排队到最后）
  ▼                            ▼
```

好处：UI 可随时 reset；已进入推理的帧不会被拔掉底，避免 SIGSEGV。替换/析构不在持锁区，避免阻塞采集线程。

**跨平台采集说明**

FFmpeg `avdevice` 屏蔽了 AVFoundation/V4L2/DShow 的差异。相较早期 Objective-C++ 直采，FFmpeg 路径还顺带消除了同翻译单元内 `AVMediaType`（AVFoundation 的 `NSString*` typedef vs FFmpeg 的 enum）的命名冲突，无需再维护 `.mm` + helper 的双文件拆分。

#### 2.5 VideoItem / I420Render

```
Controller::receiveLocalYuvData(YUVData)
                │
                ▼
         VideoItem (QQuickItem)
                │
                ▼
         I420Render (QOpenGLFunctions)
         • 3 张 OpenGL 纹理（Y/U/V 平面）
         • GLSL shader 执行 YUV → RGB 转换
         • 直接输出到 OpenGL 上下文，零额外拷贝
```

#### 2.6 AsrClient

AsrClient 识别的是 **远端** 音频（对端说话内容），不是本地麦克风。libwebrtc 通过 `AudioSinkInterface` 把解码后的远端 PCM 推回 C-ABI → `RemoteAudioCb` → `Controller::onRemoteAudioData`。

```
远端音频 RTP → libwebrtc 解码 → AudioSink
      │ RemoteAudioCb → Controller::onRemoteAudioData()
      ▼
AVFrame (AV_SAMPLE_FMT_S16, 48kHz)
      │
      ▼
AsrClient::pushAudioFrame()
      │ SwrContext 重采样 → 16kHz, S16, mono（FunASR 要求）
      ▼
QWebSocket → FunASRServer (:10095)
      │ 2pass 模式：在线流式识别 + 离线修正
      ▼
emit asrText(text, is_final)
      │
      ▼
Controller → QML AsrPanel 显示
```

### 3. 线程模型

```
主线程（Qt 事件循环）
  ├─ QML 引擎 / 渲染
  ├─ Controller（信号路由、YUV 渲染分发）
  ├─ MediaController（摄像头开关、AI enable/disable）
  └─ AsrClient（ASR 信令、WebSocket）

clientThread_（QThread）
  └─ ClientWorker
       ├─ QWebSocket（信令收发）
       └─ WebRTCClient（信令解析、SDP/ICE 处理，包括 setLocalAudioEnabled）

libwebrtc 内部线程（rtc::Thread）
  ├─ networkThread_    ICE/STUN/DTLS/SRTP
  ├─ workerThread_     编解码、媒体处理
  └─ signalingThread_  PeerConnection 状态机

VideoCapturer::captureThread_（std::thread）
  └─ av_read_frame 阻塞读 → 解码 → 色彩转换
     └─ frameCallback_ 回调（在此线程执行 gesture/seg 推理与 webrtcSink_）
```

### 4. 依赖关系图

```
main.cpp
  └─ Controller
       ├─ ClientWorker ──► WebRTCClient ──► libwebrtc.a
       │       └─ QWebSocket
       ├─ MediaController
       │       ├─ VideoCapturer (FFmpeg avformat/avcodec/avdevice + swscale)
       │       └─ VideoProcesser （shared_ptr + mutex 快照）
       │               ├─ GestureInfer (ONNX Runtime + OpenCV + FFmpeg swscale)
       │               └─ SegInfer     (ONNX Runtime + OpenCV + FFmpeg swscale)
       ├─ AsrClient ──► FunASRServer (WebSocket)
       └─ VideoItem (I420Render, OpenGL)
```

### 5. 编译链路

```
CMake GLOB_RECURSE (*.cpp *.cc *.mm)
  │
  ├─ Apple clang 编译（std::__1 ABI）
  │   所有 .cpp / .cc / .mm，除下列两个文件外
  │   （目前仅 requestPermissions.mm 使用 Objective-C++）
  │
  └─ chromium clang 18 编译（std::__Cr ABI）
      ├─ WebRTCClient.cpp     直接调用 libwebrtc PeerConnection API
      └─ WebRTCVideoSource.cpp 继承 rtc::AdaptedVideoTrackSource

chromium clang 编译时使用 third-party/libwebrtc/include 下的头文件树。
该头树是对上游 libwebrtc 头文件的精简 + 打桩：
  • 保留被我们 Pimpl 实际使用的 API 签名
  • 对其依赖、但本项目不需要的类型用前向声明或最小桩代替
  （encoded_frame.h / rtp_video_header.h / clock.h 等）
  只需满足这两个 .cpp 文件的编译，运行时的 vtable 布局由 libwebrtc.a 提供。

链接：Apple clang ld
  ├─ prebuilt/WebRTCClient.cpp.o      (std::__Cr)
  ├─ prebuilt/WebRTCVideoSource.cpp.o (std::__Cr)
  ├─ libwebrtc.a                      (std::__Cr)
  ├─ libchromium_libcxx.a             (提供 std::__Cr 符号)
  ├─ libbuiltin_video_{en,de}coder_factory.a
  ├─ librtc_internal_video_codecs.a
  ├─ librtc_simulcast_encoder_adapter.a
  ├─ avformat / avcodec / avdevice    (FFmpeg，摄像头采集)
  ├─ avutil / swscale / swresample    (FFmpeg)
  ├─ onnxruntime
  ├─ OpenCV
  ├─ Qt6 frameworks
  └─ macOS system frameworks（AVFoundation 经 avdevice 间接使用）
```

---

## WebRTCClientServer（信令服务器）

### 架构

```
QCoreApplication
  └─ SignalingServer (QObject)
       └─ QWebSocketServer (NonSecureMode, :8000)
            └─ clients: QMap<QString, QWebSocket*>
                        key = clientId（URL 路径第一段）
       └─ IceServerConfig
            └─ 从环境变量生成 STUN/TURN ice_servers
```

### 连接协议

客户端连接 URL：`ws://127.0.0.1:8000/{clientId}`

服务器无持久化状态，所有逻辑在内存 `QMap` 中维护。

客户端连接成功后，服务器会先下发 ICE 配置：

```json
{"ice_servers":[{"urls":"stun:stun.l.google.com:19302"}]}
```

如果配置了 `WEBRTC_TURN_HOST` 和 `WEBRTC_TURN_SECRET`，服务器会基于 coturn
`static-auth-secret` 生成短期 TURN 凭证，并额外下发 `turn:` / `turns:` 地址。
客户端创建 PeerConnection 时使用标准 ICE 策略，由 libwebrtc 自动选择 P2P 或 TURN relay。

### 信令消息路由

| 消息字段 | 处理逻辑 |
|----------|----------|
| `{"ice_servers": [...]}` | 客户端连接后由服务器下发，用于创建 PeerConnection |
| `{"remote_joined": id}` | 广播给所有已连接客户端 |
| `{"remote_ids": [...]}` | 仅发给新连接客户端（通知房间内已有成员） |
| `{"remote_left": id}` | 客户端断开时广播 |
| `{"id": dest, "type": "offer/answer", ...}` | 按 `id` 字段点对点转发，并将 `id` 替换为发送方 id |
| `{"id": dest, "type": "candidate", ...}` | 同上（ICE candidate 转发） |
| `{"close_pc_id": id}` | 通知目标客户端 PeerConnection 已关闭 |
| `{"enable_video"/"enable_audio": bool}` | 广播给所有其他客户端 |

### 序列图：两端建立 P2P 连接

```
Client A (:clientA)          SignalingServer          Client B (:clientB)
     │                             │                        │
     │── WS connect ──────────────►│                        │
     │                             │── remote_joined:A ────►│
     │◄── remote_ids:[] ───────────│                        │
     │                             │                        │
     │                             │── WS connect ─────────►│
     │◄── remote_joined:B ─────────│                        │
     │                             │◄─── remote_ids:[A] ────│
     │                             │                        │
     │── {id:B,type:offer,sdp} ───►│── {id:A,type:offer} ──►│
     │◄── {id:A,type:answer,sdp} ──│◄── {id:B,type:answer} ─│
     │── {id:B,type:candidate} ───►│── {id:A,candidate} ───►│
     │◄── {id:A,type:candidate} ───│◄── {id:B,candidate} ───│
     │                         (P2P 直连)                    │
```

---

## FunASRServer（语音识别服务）

### 架构

```
asyncio 事件循环
  └─ websockets.serve(:10095)
       └─ ws_serve(websocket, path)  每个连接一个协程
            ├─ async_vad()     端点检测（FSMN-VAD）
            ├─ async_asr()     离线全文识别（Paraformer）
            └─ async_asr_online() 流式增量识别（Paraformer-online）
```

### 识别模式（2pass）

```
音频帧流入
    │
    ├─►【在线模式】Paraformer-online
    │   每 chunk_interval 帧处理一次
    │   → 实时返回 {mode:"2pass-online", text, is_final:false}
    │
    ├─►【VAD】FSMN-VAD
    │   检测语音端点
    │   → speech_start / speech_end
    │
    └─►【离线模式】Paraformer + CT-Transformer 标点
        语音结束时对完整句子识别
        → 返回 {mode:"2pass-offline", text, is_final:true}
```

### 与 AsrClient 的协议

| 方向 | 格式 | 说明 |
|------|------|------|
| Client → Server | binary | S16LE, 16kHz, mono, PCM 数据块 |
| Client → Server | JSON `{"is_speaking": bool}` | 控制说话状态 |
| Server → Client | JSON `{"mode", "text", "is_final"}` | 识别结果 |
