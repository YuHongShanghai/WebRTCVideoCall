//
// Created by 余泓 on 2025/11/1.
//

#ifndef WEBRTCCLIENT_H
#define WEBRTCCLIENT_H

#include <netinet/in.h>
#include <rtc/rtc.hpp>
#include <string>

#include "VideoCapturer.h"

struct RtpDispatchConfig {
    int srcSock{0};  // 从udp端口接收本地流
    int srcPort{0};
    int sinkSock{0};  // 将收到的远端流发送到udp端口
    int sinkPort{0};
    sockaddr_in sinkAddr{};
};

class WebRTCClient {
public:
    WebRTCClient();
    ~WebRTCClient();
    void connectSignalServer();
    void call(std::string id);
    void hungup(bool first = true);
    void sendMessage(const std::string &msg);
    void setRoomClientsCallback(std::function<void(std::string)> callback);
    void setPcStateCallback(std::function<void(rtc::PeerConnection::State)> callback);
    void setRemoteCallCallback(std::function<void(std::string)> callback);
    void setRemoteMessageCallback(std::function<void(std::string)> callback);
    std::string localId();
    int videoSrcPort();
    int videoSinkPort();
    int audioSrcPort();
    int audioSinkPort();

private:
    static std::string randomId(size_t length);
    rtc::Configuration getRtcConfiguration();
    void createPeerConnection(std::weak_ptr<rtc::WebSocket> wws, std::string id);
    void setupDataChannel();
    bool setupSrcRtp(int &sock, int port, int &err);
    bool setupSinkRtp(int &sock, int port, sockaddr_in &addr, int &err);

    void addMediaTrack();
    void addVideoTrack();
    void addAudioTrack();

    void sendVideoToRemote();
    void sendAudioToRemote();
    void stopSendMedia();

    void onMessage(std::string &msg);

    std::string localId_;
    std::shared_ptr<rtc::WebSocket> ws_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::unordered_map<std::string, std::shared_ptr<rtc::PeerConnection>> peerConnectionMap_;

    std::function<void(std::string)> roomClientsCallback_;
    std::function<void(rtc::PeerConnection::State)> pcStateCallback_;
    std::function<void(std::string)> remoteCallCallback_;
    std::function<void(std::string)> remoteMessageCallback_;

    bool isCaller_ = false;

    const int BUFFER_SIZE = 2*1024*1024;

    // 视频
    std::shared_ptr<rtc::Track> videoTrack_;
    RtpDispatchConfig videoRtpConfig_;
    rtc::SSRC videoSsrc_ = 42;
    std::thread *sendVideoThread_ = nullptr;
    std::atomic<bool> stopSendMedia_ = false;

    // 音频
    std::shared_ptr<rtc::Track> audioTrack_;
    RtpDispatchConfig audioRtpConfig_;
    rtc::SSRC audioSsrc_ = 52;
    std::thread *sendAudioThread_ = nullptr;

    // datachannel
    std::shared_ptr<rtc::DataChannel> dc_;
};



#endif //WEBRTCCLIENT_H
