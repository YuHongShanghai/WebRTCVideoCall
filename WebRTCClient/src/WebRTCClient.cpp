// libwebrtc headers — must come before any Qt / system C++ headers
// (Qt redefines `emit`, which conflicts with sigslot.h's emit() method)
// Include modules/* first so the types used in scoped_refptr<> destructors
// (AudioProcessing, AudioDeviceModule) are complete when the factory header
// expands them via template instantiation below.
#include "modules/audio_device/include/audio_device.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/audio_options.h"
#include "api/create_peerconnection_factory.h"
#include "api/data_channel_interface.h"
#include "api/jsep.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/set_local_description_observer_interface.h"
#include "api/set_remote_description_observer_interface.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"

#include "WebRTCClient.h"
#include "WebRTCVideoSource.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}

// NOTE: ClientInfo uses std::__1 ABI (Apple clang) and cannot be called from
// this translation unit (chromium clang, std::__Cr ABI).
// ClientInfo calls are done in ClientWorker.cpp instead.
#include "Logger.h"
#include "config.h"

using nlohmann::json;

// ══════════════════════════════════════════════════════════
// 工具函数
// ══════════════════════════════════════════════════════════

static std::string makeRandomId(size_t length) {
    using std::chrono::high_resolution_clock;
    static thread_local std::mt19937 rng(
        static_cast<unsigned>(
            high_resolution_clock::now().time_since_epoch().count()));
    static const std::string chars(
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    std::string id(length, '0');
    std::uniform_int_distribution<int> dist(0, static_cast<int>(chars.size()) - 1);
    for (auto &c : id) c = chars[dist(rng)];
    return id;
}

// ══════════════════════════════════════════════════════════
// Pimpl — 所有包含 libwebrtc / std::__Cr 类型的内部实现
// ══════════════════════════════════════════════════════════

// Forward-declare file-scope observer classes before Impl so friend decls work.
class LocalSdpSentObserver;
class AnswerAfterRemoteObserver;

struct WebRTCClient::Impl
    : public webrtc::PeerConnectionObserver
    , public webrtc::DataChannelObserver {

    // ── 内部辅助观察者（前向声明）─────────────────────────
    class CreateSdpObserver;
    friend class LocalSdpSentObserver;
    friend class AnswerAfterRemoteObserver;
    friend class CreateSdpObserver;

    // ── 线程 ──────────────────────────────────────────────
    std::unique_ptr<rtc::Thread> networkThread_;
    std::unique_ptr<rtc::Thread> workerThread_;
    std::unique_ptr<rtc::Thread> signalingThread_;

    // ── WebRTC 核心对象 ───────────────────────────────────
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pcFactory_;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface>        pc_;
    rtc::scoped_refptr<webrtc::DataChannelInterface>           dc_;
    rtc::scoped_refptr<WebRTCVideoSource>                      videoSource_;

    // ── 远端媒体 Sink ─────────────────────────────────────
    class VideoSinkImpl;
    class AudioSinkImpl;
    std::unique_ptr<VideoSinkImpl> videoSink_;
    std::unique_ptr<AudioSinkImpl> audioSink_;

    // ── 状态 ─────────────────────────────────────────────
    std::string localId_;
    std::string remoteId_;
    bool        isCaller_          = false;
    bool        localAudioEnabled_ = true;  // 发送方本地音频 track.enabled()

    // ── C 回调（不跨越 ABI 边界）─────────────────────────
    WsSendCallback      wsSendCb_     = nullptr;  void* wsSendUd_      = nullptr;
    RoomClientsCallback roomCb_       = nullptr;  void* roomUd_        = nullptr;
    PcStateCallback     pcStateCb_    = nullptr;  void* pcStateUd_     = nullptr;
    RemoteCallCb        remoteCallCb_ = nullptr;  void* remoteCallUd_  = nullptr;
    RemoteDataCb        remoteDataCb_ = nullptr;  void* remoteDataUd_  = nullptr;
    RemoteVideoCb       remoteVideoCb_= nullptr;  void* remoteVideoUd_ = nullptr;
    RemoteAudioCb       remoteAudioCb_= nullptr;  void* remoteAudioUd_ = nullptr;

    // ── PeerConnectionObserver ────────────────────────────
    void OnSignalingChange(
        webrtc::PeerConnectionInterface::SignalingState) override;
    void OnDataChannel(
        rtc::scoped_refptr<webrtc::DataChannelInterface> dc) override;
    void OnIceGatheringChange(
        webrtc::PeerConnectionInterface::IceGatheringState) override;
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
    void OnConnectionChange(
        webrtc::PeerConnectionInterface::PeerConnectionState state) override;
    void OnTrack(
        rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
    void OnRenegotiationNeeded() override {}
    void OnIceConnectionChange(
        webrtc::PeerConnectionInterface::IceConnectionState) override {}

    // ── DataChannelObserver ───────────────────────────────
    void OnStateChange() override;
    void OnMessage(const webrtc::DataBuffer& buffer) override;

    // ── 内部辅助 ─────────────────────────────────────────
    void init();
    void createPeerConnection(const std::string& remoteId);
    void addMediaTracks();
    void setupDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> dc);
    void closePeerConnection();
    void onWsMessage(const std::string& raw);
    void call(const std::string& id);
    void hungup(bool first);
    void sendData(const std::string& msg);
    void sendWsMessage(const std::string& msg);

    // 发出信令消息的便捷函数
    void sendViaWs(const std::string& msg) {
        if (wsSendCb_) wsSendCb_(msg.c_str(), wsSendUd_);
    }
};

// ══════════════════════════════════════════════════════════
// 观察者实现
// ══════════════════════════════════════════════════════════

class LocalSdpSentObserver
    : public webrtc::SetLocalDescriptionObserverInterface {
public:
    LocalSdpSentObserver(WebRTCClient::Impl* impl, std::string type, std::string sdp)
        : impl_(impl), type_(std::move(type)), sdp_(std::move(sdp)) {}

    void OnSetLocalDescriptionComplete(webrtc::RTCError error) override {
        if (!error.ok()) { Loge("SetLocalDescription failed: {}", error.message()); return; }
        json msg = {{"id", impl_->remoteId_}, {"type", type_}, {"description", sdp_}};
        Logd("Sending SDP type={} to {}", type_, impl_->remoteId_);
        impl_->sendViaWs(msg.dump());
    }
private:
    WebRTCClient::Impl* impl_;
    std::string type_, sdp_;
};

class WebRTCClient::Impl::CreateSdpObserver
    : public webrtc::CreateSessionDescriptionObserver {
public:
    explicit CreateSdpObserver(WebRTCClient::Impl* impl) : impl_(impl) {}
    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
        std::string sdp, type;
        desc->ToString(&sdp);
        type = desc->type();
        Logd("CreateSdpObserver::OnSuccess type={}", type);
        auto obs = rtc::make_ref_counted<LocalSdpSentObserver>(impl_, type, sdp);
        impl_->pc_->SetLocalDescription(
            std::unique_ptr<webrtc::SessionDescriptionInterface>(desc), obs);
    }
    void OnFailure(webrtc::RTCError error) override {
        Loge("CreateSdpObserver::OnFailure: {}", error.message());
    }
private:
    WebRTCClient::Impl* impl_;
};

class AnswerAfterRemoteObserver
    : public webrtc::SetRemoteDescriptionObserverInterface {
public:
    explicit AnswerAfterRemoteObserver(WebRTCClient::Impl* impl) : impl_(impl) {}
    void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override {
        if (!error.ok()) { Loge("SetRemoteDescription(offer) failed: {}", error.message()); return; }
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions opts;
        auto obs = rtc::make_ref_counted<WebRTCClient::Impl::CreateSdpObserver>(impl_);
        impl_->pc_->CreateAnswer(obs.get(), opts);
    }
private:
    WebRTCClient::Impl* impl_;
};

class SimpleSetRemoteObserver
    : public webrtc::SetRemoteDescriptionObserverInterface {
public:
    void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override {
        if (!error.ok()) Loge("SetRemoteDescription(answer) failed: {}", error.message());
        else             Logi("SetRemoteDescription(answer) ok");
    }
};

// ── VideoSinkImpl ─────────────────────────────────────────

class WebRTCClient::Impl::VideoSinkImpl
    : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
    explicit VideoSinkImpl(RemoteVideoCb cb, void* ud) : cb_(cb), ud_(ud) {}

    void OnFrame(const webrtc::VideoFrame& frame) override {
        if (!cb_) return;
        const webrtc::I420BufferInterface* i420 =
            frame.video_frame_buffer()->GetI420();
        rtc::scoped_refptr<webrtc::I420BufferInterface> converted;
        if (!i420) { converted = frame.video_frame_buffer()->ToI420(); i420 = converted.get(); }
        if (!i420) return;

        int w = i420->width(), h = i420->height();
        AVFrame* avf = av_frame_alloc();
        avf->format = AV_PIX_FMT_YUV420P;
        avf->width  = w;
        avf->height = h;
        av_frame_get_buffer(avf, 32);
        av_image_copy_plane(avf->data[0], avf->linesize[0], i420->DataY(), i420->StrideY(), w, h);
        av_image_copy_plane(avf->data[1], avf->linesize[1], i420->DataU(), i420->StrideU(), (w+1)/2, (h+1)/2);
        av_image_copy_plane(avf->data[2], avf->linesize[2], i420->DataV(), i420->StrideV(), (w+1)/2, (h+1)/2);
        cb_(avf, ud_);
    }
private:
    RemoteVideoCb cb_;
    void*         ud_;
};

// ── AudioSinkImpl ─────────────────────────────────────────

class WebRTCClient::Impl::AudioSinkImpl : public webrtc::AudioTrackSinkInterface {
public:
    explicit AudioSinkImpl(RemoteAudioCb cb, void* ud) : cb_(cb), ud_(ud) {}

    void OnData(const void* data, int bits, int rate,
                size_t channels, size_t frames) override {
        if (cb_) cb_(data, bits, rate, channels, frames, ud_);
    }
private:
    RemoteAudioCb cb_;
    void*         ud_;
};

// ══════════════════════════════════════════════════════════
// Impl 方法实现
// ══════════════════════════════════════════════════════════

void WebRTCClient::Impl::init() {
    networkThread_   = rtc::Thread::CreateWithSocketServer();
    workerThread_    = rtc::Thread::Create();
    signalingThread_ = rtc::Thread::Create();
    networkThread_->SetName("rtc_network",   nullptr);
    workerThread_->SetName("rtc_worker",     nullptr);
    signalingThread_->SetName("rtc_signaling", nullptr);
    networkThread_->Start();
    workerThread_->Start();
    signalingThread_->Start();

    // 3A（AEC/AGC/NS）由 libwebrtc voice engine 通过 cricket::AudioOptions 默认值处理，
    // addMediaTracks() 中 cricket::AudioOptions() 的 echo_cancellation/auto_gain_control/
    // noise_suppression 均默认为 true，无需手动创建 AudioProcessing 对象。
    pcFactory_ = webrtc::CreatePeerConnectionFactory(
        networkThread_.get(), workerThread_.get(), signalingThread_.get(),
        /* adm */ nullptr,
        webrtc::CreateBuiltinAudioEncoderFactory(),
        webrtc::CreateBuiltinAudioDecoderFactory(),
        webrtc::CreateBuiltinVideoEncoderFactory(),
        webrtc::CreateBuiltinVideoDecoderFactory(),
        /* audio_mixer */ nullptr, /* audio_processing */ nullptr);

    if (!pcFactory_) { Loge("CreatePeerConnectionFactory failed"); return; }

    videoSource_ = rtc::make_ref_counted<WebRTCVideoSource>();
    Logi("WebRTCClient initialized, localId={}", localId_);
}

void WebRTCClient::Impl::onWsMessage(const std::string& raw) {
    json msg;
    if (!json::accept(raw)) { Loge("onWsMessage: JSON parse error"); return; }
    msg = json::parse(raw);

    if (!msg.contains("id")) {
        if (roomCb_) roomCb_(raw.c_str(), roomUd_);
        return;
    }

    auto id   = msg["id"].get<std::string>();
    auto type = msg.value("type", std::string{});

    if (type == "offer") {
        Logi("Received offer from {}", id);
        remoteId_ = id; isCaller_ = false;
        // ClientInfo::instance()->setIsCaller(false); // done in ClientWorker
        if (remoteCallCb_) remoteCallCb_(id.c_str(), remoteCallUd_);

        createPeerConnection(id);
        addMediaTracks();

        webrtc::SdpParseError err;
        auto sdp  = msg["description"].get<std::string>();
        auto desc = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, sdp, &err);
        if (!desc) { Loge("CreateSessionDescription(offer) failed: {}", err.description); return; }
        pc_->SetRemoteDescription(std::move(desc), rtc::make_ref_counted<AnswerAfterRemoteObserver>(this));

    } else if (type == "answer") {
        Logi("Received answer from {}", id);
        webrtc::SdpParseError err;
        auto sdp  = msg["description"].get<std::string>();
        auto desc = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, sdp, &err);
        if (!desc) { Loge("CreateSessionDescription(answer) failed: {}", err.description); return; }
        pc_->SetRemoteDescription(std::move(desc), rtc::make_ref_counted<SimpleSetRemoteObserver>());

    } else if (type == "candidate") {
        if (!pc_) return;
        webrtc::SdpParseError err;
        auto sdp = msg["candidate"].get<std::string>();
        auto mid = msg["mid"].get<std::string>();
        auto cand = webrtc::CreateIceCandidate(mid, 0, sdp, &err);
        if (!cand) { Loge("CreateIceCandidate failed: {}", err.description); return; }
        pc_->AddIceCandidate(cand);
        delete cand;
    }
}

void WebRTCClient::Impl::call(const std::string& id) {
    if (id == localId_) return;
    remoteId_ = id; isCaller_ = true;
    // ClientInfo::instance()->setIsCaller(true); // done in ClientWorker

    createPeerConnection(id);
    addMediaTracks();

    webrtc::DataChannelInit dcConfig;
    auto result = pc_->CreateDataChannelOrError("chat", &dcConfig);
    if (result.ok()) setupDataChannel(result.MoveValue());
    else Loge("CreateDataChannel failed: {}", result.error().message());

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions opts;
    opts.offer_to_receive_audio = 1;
    opts.offer_to_receive_video = 1;
    auto obs = rtc::make_ref_counted<CreateSdpObserver>(this);
    pc_->CreateOffer(obs.get(), opts);
}

void WebRTCClient::Impl::hungup(bool first) {
    Logd("hungup first={}", first);
    if (first && !remoteId_.empty()) {
        json j; j["close_pc_id"] = remoteId_;
        sendViaWs(j.dump());
    }
    closePeerConnection();
    isCaller_ = false; remoteId_.clear();
    // ClientInfo::instance()->setIsCaller(false); // done in ClientWorker
}

void WebRTCClient::Impl::sendData(const std::string& msg) {
    if (dc_ && dc_->state() == webrtc::DataChannelInterface::kOpen) {
        webrtc::DataBuffer buf(msg);
        dc_->Send(buf);
    }
}

void WebRTCClient::Impl::sendWsMessage(const std::string& msg) { sendViaWs(msg); }

void WebRTCClient::Impl::OnSignalingChange(
    webrtc::PeerConnectionInterface::SignalingState state) {
    Logd("SignalingState: {}", static_cast<int>(state));
}

void WebRTCClient::Impl::OnDataChannel(
    rtc::scoped_refptr<webrtc::DataChannelInterface> dc) {
    Logi("OnDataChannel label={}", dc->label());
    setupDataChannel(dc);
}

void WebRTCClient::Impl::OnIceGatheringChange(
    webrtc::PeerConnectionInterface::IceGatheringState state) {
    Logd("IceGatheringState: {}", static_cast<int>(state));
}

void WebRTCClient::Impl::OnIceCandidate(
    const webrtc::IceCandidateInterface* candidate) {
    std::string sdp;
    candidate->ToString(&sdp);
    json msg = {{"id", remoteId_}, {"type","candidate"}, {"candidate",sdp}, {"mid",candidate->sdp_mid()}};
    if (wsSendCb_) wsSendCb_(msg.dump().c_str(), wsSendUd_);
}

void WebRTCClient::Impl::OnConnectionChange(
    webrtc::PeerConnectionInterface::PeerConnectionState state) {
    Logi("PeerConnectionState: {}", static_cast<int>(state));
    if (pcStateCb_) pcStateCb_(static_cast<int>(state), pcStateUd_);
}

void WebRTCClient::Impl::OnTrack(
    rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
    auto receiver = transceiver->receiver();
    if (!receiver) return;
    auto track = receiver->track();
    if (!track) return;

    if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
        Logi("OnTrack: video");
        videoSink_ = std::make_unique<VideoSinkImpl>(remoteVideoCb_, remoteVideoUd_);
        static_cast<webrtc::VideoTrackInterface*>(track.get())
            ->AddOrUpdateSink(videoSink_.get(), rtc::VideoSinkWants{});
    } else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
        Logi("OnTrack: audio");
        audioSink_ = std::make_unique<AudioSinkImpl>(remoteAudioCb_, remoteAudioUd_);
        static_cast<webrtc::AudioTrackInterface*>(track.get())
            ->AddSink(audioSink_.get());
    }
}

void WebRTCClient::Impl::OnStateChange() {
    if (!dc_) return;
    Logi("DataChannel state: {}", static_cast<int>(dc_->state()));
}

void WebRTCClient::Impl::OnMessage(const webrtc::DataBuffer& buffer) {
    if (!buffer.binary && remoteDataCb_) {
        std::string msg(buffer.data.data<char>(), buffer.data.size());
        remoteDataCb_(msg.c_str(), remoteDataUd_);
    }
}

void WebRTCClient::Impl::createPeerConnection(const std::string& remoteId) {
    remoteId_ = remoteId;
    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    webrtc::PeerConnectionInterface::IceServer stun;
    stun.uri = STUN_SERVER;
    config.servers.push_back(std::move(stun));
    webrtc::PeerConnectionDependencies deps(this);
    auto result = pcFactory_->CreatePeerConnectionOrError(config, std::move(deps));
    if (!result.ok()) { Loge("CreatePeerConnection failed: {}", result.error().message()); return; }
    pc_ = result.MoveValue();
    Logi("PeerConnection created for {}", remoteId);
}

void WebRTCClient::Impl::addMediaTracks() {
    if (!pc_ || !pcFactory_) return;
    auto vTrack = pcFactory_->CreateVideoTrack(videoSource_, "video0");
    if (auto r = pc_->AddTrack(vTrack, {"stream0"}); !r.ok())
        Loge("AddVideoTrack failed: {}", r.error().message());

    cricket::AudioOptions audioOpts;
    audioOpts.echo_cancellation = true;
    audioOpts.auto_gain_control = true;
    audioOpts.noise_suppression = true;
    audioOpts.highpass_filter   = true;
    auto aSource = pcFactory_->CreateAudioSource(audioOpts);
    auto aTrack  = pcFactory_->CreateAudioTrack("audio0", aSource.get());
    // 把当前静音状态应用到新建的 track，保证“呼叫时即静音”语义
    aTrack->set_enabled(localAudioEnabled_);
    if (auto r = pc_->AddTrack(aTrack, {"stream0"}); !r.ok())
        Loge("AddAudioTrack failed: {}", r.error().message());
}

void WebRTCClient::Impl::setupDataChannel(
    rtc::scoped_refptr<webrtc::DataChannelInterface> dc) {
    dc_ = std::move(dc);
    dc_->RegisterObserver(this);
}

void WebRTCClient::Impl::closePeerConnection() {
    if (pc_) {
        for (auto& trans : pc_->GetTransceivers()) {
            auto recv = trans->receiver();
            if (!recv) continue;
            auto track = recv->track();
            if (!track) continue;
            if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
                if (videoSink_)
                    static_cast<webrtc::VideoTrackInterface*>(track.get())
                        ->RemoveSink(videoSink_.get());
            } else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
                if (audioSink_)
                    static_cast<webrtc::AudioTrackInterface*>(track.get())
                        ->RemoveSink(audioSink_.get());
            }
        }
    }
    videoSink_.reset(); audioSink_.reset();
    if (dc_) { dc_->UnregisterObserver(); dc_->Close(); dc_ = nullptr; }
    if (pc_) { pc_->Close(); pc_ = nullptr; }
}

// ══════════════════════════════════════════════════════════
// WebRTCClient 公共接口（通过 Pimpl 转发）
// ══════════════════════════════════════════════════════════

WebRTCClient::WebRTCClient() : d_(new Impl()) {
    d_->localId_ = makeRandomId(6);
    // ClientInfo::instance()->setLocalId(d_->localId_); // done in ClientWorker
    Logi("Local Id: {}", d_->localId_);
}

WebRTCClient::~WebRTCClient() {
    d_->closePeerConnection();
    d_->pcFactory_ = nullptr;
    if (d_->signalingThread_) d_->signalingThread_->Stop();
    if (d_->workerThread_)    d_->workerThread_->Stop();
    if (d_->networkThread_)   d_->networkThread_->Stop();
    delete d_;
}

void WebRTCClient::init()                { d_->init(); }
const char* WebRTCClient::localId() const { return d_->localId_.c_str(); }

void WebRTCClient::setWsSendCallback    (WsSendCallback      cb, void* ud) { d_->wsSendCb_      = cb; d_->wsSendUd_      = ud; }
void WebRTCClient::setRoomClientsCallback(RoomClientsCallback cb, void* ud) { d_->roomCb_        = cb; d_->roomUd_        = ud; }
void WebRTCClient::setPcStateCallback   (PcStateCallback     cb, void* ud) { d_->pcStateCb_     = cb; d_->pcStateUd_     = ud; }
void WebRTCClient::setRemoteCallCallback(RemoteCallCb        cb, void* ud) { d_->remoteCallCb_  = cb; d_->remoteCallUd_  = ud; }
void WebRTCClient::setRemoteDataCallback(RemoteDataCb        cb, void* ud) { d_->remoteDataCb_  = cb; d_->remoteDataUd_  = ud; }
void WebRTCClient::setRemoteVideoCallback(RemoteVideoCb      cb, void* ud) { d_->remoteVideoCb_ = cb; d_->remoteVideoUd_ = ud; }
void WebRTCClient::setRemoteAudioCallback(RemoteAudioCb      cb, void* ud) { d_->remoteAudioCb_ = cb; d_->remoteAudioUd_ = ud; }

void WebRTCClient::onWsMessage (const char* msg)  { d_->onWsMessage(msg);  }
void WebRTCClient::sendWsMessage(const char* msg)  { d_->sendViaWs(msg);   }
void WebRTCClient::call        (const char* id)   { d_->call(id);          }
void WebRTCClient::hungup      (bool first)        { d_->hungup(first);    }
void WebRTCClient::sendData    (const char* msg)   { d_->sendData(msg);    }
void WebRTCClient::pushVideoFrame(AVFrame* frame)  {
    if (d_->videoSource_) d_->videoSource_->pushFrame(frame);
}

void WebRTCClient::setLocalAudioEnabled(bool enabled) {
    d_->localAudioEnabled_ = enabled;
    if (!d_->pc_) return;  // 尚未建连：状态已记录，addMediaTracks() 会应用
    for (auto& sender : d_->pc_->GetSenders()) {
        auto track = sender->track();
        if (!track) continue;
        if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
            track->set_enabled(enabled);
        }
    }
}
