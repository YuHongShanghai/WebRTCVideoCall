// Stubbed for bridge compilation — real definition lives in libwebrtc.a.
// The original header transitively required modules/rtp_rtcp/source and
// modules/video_coding/include headers that are not shipped with this
// prebuilt tree. Bridge translation units (WebRTCClient.cpp,
// WebRTCVideoSource.cpp) never reference EncodedFrame directly, so a
// forward declaration is enough to satisfy parsing.
#ifndef API_VIDEO_ENCODED_FRAME_H_
#define API_VIDEO_ENCODED_FRAME_H_

#include "api/video/encoded_image.h"

namespace webrtc {
class EncodedFrame;
}  // namespace webrtc

#endif  // API_VIDEO_ENCODED_FRAME_H_
