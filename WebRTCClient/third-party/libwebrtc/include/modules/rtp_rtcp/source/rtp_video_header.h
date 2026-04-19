// Minimal stub for modules/rtp_rtcp/source/rtp_video_header.h.
// The real definition is a large struct used only inside libwebrtc.a.
// Bridge translation units (WebRTCClient.cpp) only reference RTPVideoHeader
// through pointer parameters in virtual interfaces they never call, so an
// empty struct is ABI-safe here — no code in the bridge TU instantiates it.
#ifndef MODULES_RTP_RTCP_SOURCE_RTP_VIDEO_HEADER_H_
#define MODULES_RTP_RTCP_SOURCE_RTP_VIDEO_HEADER_H_

namespace webrtc {
struct RTPVideoHeader;
}  // namespace webrtc

#endif  // MODULES_RTP_RTCP_SOURCE_RTP_VIDEO_HEADER_H_
