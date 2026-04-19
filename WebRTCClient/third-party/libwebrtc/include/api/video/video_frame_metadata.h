// Stubbed for bridge compilation — real definition lives in libwebrtc.a.
// The original header pulls in modules/video_coding/codecs/{h264,vp8,vp9}
// globals which are not shipped in this prebuilt tree. Bridge translation
// units never reference VideoFrameMetadata directly, so a forward declaration
// is enough to satisfy transitive includes through frame_transformer_interface.h.
#ifndef API_VIDEO_VIDEO_FRAME_METADATA_H_
#define API_VIDEO_VIDEO_FRAME_METADATA_H_

namespace webrtc {
class VideoFrameMetadata;
}  // namespace webrtc

#endif  // API_VIDEO_VIDEO_FRAME_METADATA_H_
