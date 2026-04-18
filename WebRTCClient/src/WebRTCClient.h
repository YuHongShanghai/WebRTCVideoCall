#pragma once

// ──────────────────────────────────────────────────────────
// WebRTCClient — 基于原生 libwebrtc 的 P2P 客户端
//
// 公共接口故意只使用 C-ABI 兼容类型（const char*, 函数指针, POD）。
// 这样可以让此头文件在使用 std::__1（Apple clang）和 std::__Cr
// （chromium clang, 用于编译 libwebrtc.a）的编译单元之间安全共享。
//
// 信令（WebSocket）由上层 ClientWorker (QObject) 负责；
// 本类通过 wsSendCallback 发送，onWsMessage() 接收。
// ──────────────────────────────────────────────────────────

extern "C" {
#include <libavutil/frame.h>
}
#include <cstddef>  // size_t

// ── C-兼容回调类型 ─────────────────────────────────────────
// 使用原始函数指针 + void* userData，避免 std::function 的 ABI 问题
using WsSendCallback     = void (*)(const char* msg,  void* userData);
using RoomClientsCallback= void (*)(const char* json, void* userData);
using PcStateCallback    = void (*)(int state,         void* userData);
using RemoteCallCb       = void (*)(const char* id,   void* userData);
using RemoteDataCb       = void (*)(const char* data, void* userData);
using RemoteVideoCb      = void (*)(AVFrame* frame,   void* userData);
using RemoteAudioCb      = void (*)(const void* data, int bits, int rate,
                                    size_t channels, size_t frames,
                                    void* userData);

// ── 前向声明（隐藏 C++ ABI 相关的 libwebrtc 类型）──────────
// 头文件内不包含任何 libwebrtc 头文件，避免 Qt moc 看到 sigslot.h
class WebRTCVideoSource;

class WebRTCClient {
public:
    WebRTCClient();
    ~WebRTCClient();

    // ── 生命周期 ──────────────────────────────────────────
    void init();
    const char* localId() const;   // 指向内部存储，生命周期同 WebRTCClient

    // ── 回调注册（必须在 init() 前调用）───────────────────
    void setWsSendCallback    (WsSendCallback      cb, void* ud);
    void setRoomClientsCallback(RoomClientsCallback cb, void* ud);
    void setPcStateCallback   (PcStateCallback     cb, void* ud);
    void setRemoteCallCallback(RemoteCallCb        cb, void* ud);
    void setRemoteDataCallback(RemoteDataCb        cb, void* ud);
    void setRemoteVideoCallback(RemoteVideoCb      cb, void* ud);
    void setRemoteAudioCallback(RemoteAudioCb      cb, void* ud);

    // ── 信令 ─────────────────────────────────────────────
    void onWsMessage(const char* msg);
    void sendWsMessage(const char* msg);

    // ── 呼叫控制 ──────────────────────────────────────────
    void call   (const char* id);
    void hungup (bool first = true);
    void sendData(const char* msg);

    // ── 视频输入 ─────────────────────────────────────────
    void pushVideoFrame(AVFrame* frame);

    // Pimpl：隐藏所有包含 std::__Cr 类型的 libwebrtc 成员
    // 声明为 public 供 .cpp 文件中的 file-scope observer 类引用 Impl*
    struct Impl;

private:
    Impl* d_;
};
