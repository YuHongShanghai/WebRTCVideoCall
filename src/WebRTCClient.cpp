//
// Created by 余泓 on 2025/11/1.
//

#include "WebRTCClient.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <random>
#include <sys/socket.h>
#include <unistd.h>

#include "ClientInfo.h"
#include "Logger.h"
#include "config.h"
#include "magic_enum.hpp"

using nlohmann::json;
template<class T>
std::weak_ptr<T> make_weak_ptr(std::shared_ptr<T> ptr) {
    return ptr;
}

WebRTCClient::WebRTCClient() {
    rtc::InitLogger(rtc::LogLevel::Debug);
    localId_ = randomId(6);
    ClientInfo::instance()->setLocalId(localId_);
    Logi("Local Id: {}", localId_);
}

WebRTCClient::~WebRTCClient() {
    stopSendMedia();

    if (ws_.get()) {
        ws_->close();
    }
}

void WebRTCClient::connectSignalServer() {
    ws_ = std::make_shared<rtc::WebSocket>();

    std::promise<void> wsPromise;
    auto wsFuture = wsPromise.get_future();

    // 信令服务器连接成功回调
    ws_->onOpen([&wsPromise]() {
        Logi("WebSocket connected, signaling ready");
        wsPromise.set_value();
    });

    // 信令服务器连接失败回调
    ws_->onError([&wsPromise](std::string s) {
        Loge("WebSocket error: {}", s);
        wsPromise.set_exception(std::make_exception_ptr(std::runtime_error(s)));
    });

    // 断开连接回调
    ws_->onClosed([]() { Logi("WebSocket closed"); });

    // 接收来自其他客户端的信令消息（JSON 格式）
    ws_->onMessage([wws = make_weak_ptr(ws_), this](auto data) {
        // data holds either std::string or rtc::binary
        if (!std::holds_alternative<std::string>(data))
            return;

        json message = json::parse(std::get<std::string>(data));

        auto it = message.find("id");
        if (it == message.end()) {
            // 自定义消息
            if (roomClientsCallback_) {
                roomClientsCallback_(std::get<std::string>(data));
            }
            return;
        }


        auto id = it->get<std::string>();

        it = message.find("type");
        if (it == message.end())
            return;

        auto type = it->get<std::string>();
        /*
        可能的 type 有三种：
            "offer"：对方发来 SDP offer，我们要创建 PeerConnection 并生成 answer；
            "answer"：对方响应了我们的 offer；
            "candidate"：对方发来新的 ICE candidate。
        */
        std::shared_ptr<rtc::PeerConnection> pc;
        if (auto jt = peerConnectionMap_.find(id); jt != peerConnectionMap_.end()) {
            pc = jt->second; // 已经有和对方的连接，直接用
        } else if (type == "offer") {
            // 没有连接且对方发来 SDP offer，先创建 PeerConnection
            Logi("Answering to {}", id);
            if (remoteCallCallback_) {
                remoteCallCallback_(id);
            }
            try {
                createPeerConnection(wws, id);
            } catch (std::exception &e) {
                Loge("Exception occured while creating peerconnection: {}", e.what());
                return;
            }
            addMediaTrack();
        } else {
            // 其他情况，即没有建立连接，但是对方发了不是"offer"类型的消息，直接不处理
            return;
        }

        if (type == "offer" || type == "answer") {
            // 作为caller，收到对方的answer后
            if (type == "answer") {
                Logi("received answer from " + id);
            }
            if (type == "offer") {
                Logi("received offer from " + id);
            }

            // 对方发送过来SDP offer或者对方响应了我们的 offer，setRemoteDescription
            // setRemoteDescription 中，当type类型是offer时，接口内部会：
            //  1. 自动调用内部的 createAnswer();
            //  2. 自动设置本地 SDP(setLocalDescription(answer));
            //  3. 触发回调 onLocalDescription(answer)。
            auto sdp = message["description"].get<std::string>();
            pc_->setRemoteDescription(rtc::Description(sdp, type));
        } else if (type == "candidate") {
            // 对方发来新的 ICE candidate，addRemoteCandidate
            auto sdp = message["candidate"].get<std::string>();
            auto mid = message["mid"].get<std::string>();
            pc_->addRemoteCandidate(rtc::Candidate(sdp, mid));
        }
    });
    ws_->open(std::string(WS_SERVER) + "/" + localId_);
    Logi("Waiting for signaling to be connected...");
    try {
        wsFuture.get(); // 阻塞直到连接到服务器
    } catch (std::runtime_error &e) {
        Loge("runtime_error {}", e.what());
    }
}

// Create and setup a PeerConnection
void WebRTCClient::createPeerConnection(std::weak_ptr<rtc::WebSocket> wws,
                                        std::string id) {
    pc_ = std::make_shared<rtc::PeerConnection>(getRtcConfiguration());

    pc_->onStateChange([this](rtc::PeerConnection::State state) {
        Logi("peer connection state: {}", magic_enum::enum_name(state));
        if (state == rtc::PeerConnection::State::Connected) {
            stopSendMedia_ = false;
            sendVideoThread_ = new std::thread([this]() {
                sendVideoToRemote();
            });
            sendAudioThread_ = new std::thread([this]() {
                sendAudioToRemote();
            });
        }
        if (pcStateCallback_) {
            pcStateCallback_(state);
        }
    });

    pc_->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
        Logi("Gathering State: {}", magic_enum::enum_name(state));
        // if (state == rtc::PeerConnection::GatheringState::Complete) {
        // 	auto description = pc_->localDescription();
        // 	std::cout << "local description: " << std::string(description.value()) << std::endl;
        // }
    });

    // 本地 SDP (offer or answer)生成后回调，发送给对端
    pc_->onLocalDescription([wws, id](rtc::Description description) {
        json message = {{"id", id}, {"type", description.typeString()}, {"description", std::string(description)}};

        Logd("onLocalDescription: {}", message.dump());

        if (auto ws = wws.lock())
            ws->send(message.dump());
    });

    pc_->onTrack([](auto track) {
        Logd("onTrack: {}");
    });

    // 本地ICE生成时回调，发送给对端
    // pc->createDataChannel() 或 setRemoteDescription() 时触发
    // 所有候选收集完毕，触发 onGatheringStateChange(Complete)
    pc_->onLocalCandidate([wws, id](rtc::Candidate candidate) {
        json message = {
                {"id", id}, {"type", "candidate"}, {"candidate", std::string(candidate)}, {"mid", candidate.mid()}};

        Logd("onLocalCandidate: {}", message.dump());

        if (auto ws = wws.lock())
            ws->send(message.dump());
    });

    peerConnectionMap_.emplace(id, pc_);
    Logd("end");
}

bool WebRTCClient::setupSrcRtp(int &sock, int port, int &err) {
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);

    timeval tv;
    tv.tv_sec = 1;        // 1 秒超时
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (bind(sock, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) < 0) {
        err = errno;
        return false;
    }

    int rcvBufSize = BUFFER_SIZE;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&rcvBufSize), sizeof(rcvBufSize));
    return true;
}

void WebRTCClient::sendVideoToRemote() {
    // Receive from UDP
    char *buffer = new char[BUFFER_SIZE];
    int len;
    while (!stopSendMedia_) {
        len = recv(videoRtpConfig_.srcSock, buffer, BUFFER_SIZE, 0);
        if (len < 0) {
            continue;
        }
        if (len < sizeof(rtc::RtpHeader) || !videoTrack_->isOpen())
            continue;

        auto rtp = reinterpret_cast<rtc::RtpHeader *>(buffer);
        rtp->setSsrc(videoSsrc_);
        // Logd("sendVideoToRemote");
        videoTrack_->send(reinterpret_cast<const std::byte *>(buffer), len);
    }
    delete[] buffer;
}

void WebRTCClient::sendAudioToRemote() {
    // Receive from UDP
    char *buffer = new char[BUFFER_SIZE];
    int len;
    while (!stopSendMedia_) {
        len = recv(audioRtpConfig_.srcSock, buffer, BUFFER_SIZE, 0);
        if (len < 0) {
            continue;
        }
        if (len < sizeof(rtc::RtpHeader) || !audioTrack_->isOpen()) {
            continue;
        }

        auto rtp = reinterpret_cast<rtc::RtpHeader *>(buffer);
        rtp->setSsrc(audioSsrc_);
        audioTrack_->send(reinterpret_cast<const std::byte *>(buffer), len);
    }
    delete[] buffer;
}

void WebRTCClient::stopSendMedia() {
    Logd("start");
    stopSendMedia_ = true;
    if (videoRtpConfig_.srcSock > 0) {
        ::close(videoRtpConfig_.srcSock);
        videoRtpConfig_.srcSock = -1;
    }
    if (audioRtpConfig_.srcSock > 0) {
        ::close(audioRtpConfig_.srcSock);
        audioRtpConfig_.srcSock = -1;
    }
    if (videoRtpConfig_.sinkSock > 0) {
        ::close(videoRtpConfig_.sinkSock);
        videoRtpConfig_.sinkSock = -1;
    }
    if (audioRtpConfig_.sinkSock > 0) {
        ::close(audioRtpConfig_.sinkSock);
        audioRtpConfig_.sinkSock = -1;
    }
    if (sendVideoThread_ && sendVideoThread_->joinable()) {
        sendVideoThread_->join();
    }
    sendVideoThread_ = nullptr;

    if (sendAudioThread_ && sendAudioThread_->joinable()) {
        sendAudioThread_->join();
    }
    Logd("end");
}

bool WebRTCClient::setupSinkRtp(int &sock, int port, sockaddr_in &addr, int &err) {
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    return true;
}
void WebRTCClient::addMediaTrack() {
    addVideoTrack();
    addAudioTrack();
}

void WebRTCClient::addVideoTrack() {
    videoRtpConfig_.srcPort = 6000;
    videoRtpConfig_.sinkPort = 5000;

    if (isCaller_) {
        videoRtpConfig_.srcPort += 100;
        videoRtpConfig_.sinkPort += 100;
    }

    int err = 0;
    bool success = setupSrcRtp(videoRtpConfig_.srcSock, videoRtpConfig_.srcPort, err);
    while (!success && errno == EADDRINUSE) {
        videoRtpConfig_.srcPort += 5;
        success = setupSrcRtp(videoRtpConfig_.srcSock, videoRtpConfig_.srcPort, err);
    }
    Logd("setupSrcRtp port: {}", videoRtpConfig_.srcPort);

    setupSinkRtp(videoRtpConfig_.sinkSock, videoRtpConfig_.sinkPort, videoRtpConfig_.sinkAddr, err);
    Logd("setupSinkRtp port: {}", videoRtpConfig_.sinkPort);

    rtc::Description::Video media("video", rtc::Description::Direction::SendRecv);
    media.addH264Codec(96); // Must match the payload type of the external h264 RTP stream
    media.addSSRC(videoSsrc_, isCaller_ ? " video-caller" : "video-callee");
    media.setBitrate(3000);
    videoTrack_ = pc_->addTrack(media);

    auto session = std::make_shared<rtc::RtcpReceivingSession>();
    videoTrack_->setMediaHandler(session);

    videoTrack_->onMessage(
            [this](rtc::binary message) {
                // This is an RTP packet
                sendto(videoRtpConfig_.sinkSock, reinterpret_cast<const char *>(message.data()), int(message.size()), 0,
                       reinterpret_cast<const struct sockaddr *>(&videoRtpConfig_.sinkAddr), sizeof(videoRtpConfig_.sinkAddr));
            },
            nullptr);
}

void WebRTCClient::addAudioTrack() {
    audioRtpConfig_.srcPort = 6500;
    audioRtpConfig_.sinkPort = 5500;

    if (isCaller_) {
        audioRtpConfig_.srcPort += 100;
        audioRtpConfig_.sinkPort += 100;
    }

    int err = 0;
    bool success = setupSrcRtp(audioRtpConfig_.srcSock, audioRtpConfig_.srcPort, err);
    while (!success && errno == EADDRINUSE) {
        audioRtpConfig_.srcPort += 5;
        success = setupSrcRtp(audioRtpConfig_.srcSock, audioRtpConfig_.srcPort, err);
    }
    Logd("setupSrcRtp port: {}", audioRtpConfig_.srcPort);

    setupSinkRtp(audioRtpConfig_.sinkSock, audioRtpConfig_.sinkPort, audioRtpConfig_.sinkAddr, err);
    Logd("setupSinkRtp port: {}", audioRtpConfig_.sinkPort);

    rtc::Description::Audio media("audio", rtc::Description::Direction::SendRecv);
    media.addOpusCodec(111);
    media.addSSRC(audioSsrc_, isCaller_ ? "audio-caller" : "audio-callee");
    media.setBitrate(128);
    audioTrack_ = pc_->addTrack(media);

    auto audioSession = std::make_shared<rtc::RtcpReceivingSession>();
    audioTrack_->setMediaHandler(audioSession);
    audioTrack_->onMessage(
        [this](rtc::binary message) {
            // This is an RTP packet
            sendto(audioRtpConfig_.sinkSock, reinterpret_cast<const char *>(message.data()), int(message.size()), 0,
                   reinterpret_cast<const struct sockaddr *>(&audioRtpConfig_.sinkAddr), sizeof(audioRtpConfig_.sinkAddr));
        },
        nullptr);
}

void WebRTCClient::call(std::string id) {
    if (id == localId_) {
        return;
    }

    isCaller_ = true;
    ClientInfo::instance()->setIsCaller(true);
    videoSsrc_ = 43;
    audioSsrc_ = 53;

    createPeerConnection(ws_, id);
    addMediaTrack();
    pc_->setLocalDescription();
}

void WebRTCClient::hungup(bool first) {
    Logd("hungup");

    if (ws_ && first && !peerConnectionMap_.empty()) {
        // only one pc
        json j;
        j["close_pc_id"] = peerConnectionMap_.begin()->first;
        ws_->send(j.dump());
    }

    stopSendMedia();

    if (videoTrack_) {
        videoTrack_->resetCallbacks();
        videoTrack_->close();
        videoTrack_ = nullptr;
    }

    if (audioTrack_) {
        audioTrack_->resetCallbacks();
        audioTrack_->close();
        audioTrack_ = nullptr;
    }

    if (pc_) {
        pc_->resetCallbacks();

        pc_->close();
        Logd("pc closed");
        pc_ = nullptr;
    }

    peerConnectionMap_.clear();
    rtc::Cleanup();

    isCaller_ = false;
    ClientInfo::instance()->setIsCaller(false);
}

void WebRTCClient::setRoomClientsCallback(std::function<void(std::string)> callback) {
    roomClientsCallback_ = callback;
}

void WebRTCClient::setPcStateCallback(std::function<void(rtc::PeerConnection::State)> callback) {
    pcStateCallback_ = callback;
}

void WebRTCClient::setRemoteCallCallback(std::function<void(std::string)> callback) {
    remoteCallCallback_ = callback;
}

std::string WebRTCClient::localId() { return localId_; }

int WebRTCClient::videoSrcPort() {
    return videoRtpConfig_.srcPort;
}

int WebRTCClient::videoSinkPort() { return videoRtpConfig_.sinkPort; }

int WebRTCClient::audioSrcPort() {
    return audioRtpConfig_.srcPort;
}

int WebRTCClient::audioSinkPort() {
    return audioRtpConfig_.sinkPort;
}

rtc::Configuration WebRTCClient::getRtcConfiguration() {
    rtc::Configuration config;
    config.iceServers.emplace_back(STUN_SERVER);
    return config;
}

// Helper function to generate a random ID
std::string WebRTCClient::randomId(size_t length) {
    using std::chrono::high_resolution_clock;
    static thread_local std::mt19937 rng(
            static_cast<unsigned int>(high_resolution_clock::now().time_since_epoch().count()));
    static const std::string characters("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    std::string id(length, '0');
    std::uniform_int_distribution<int> uniform(0, int(characters.size() - 1));
    std::generate(id.begin(), id.end(), [&]() { return characters.at(uniform(rng)); });
    return id;
}
