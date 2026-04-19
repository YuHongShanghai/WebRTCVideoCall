#ifndef CALL_AUDIO_STATE_H_
#define CALL_AUDIO_STATE_H_
#include "api/scoped_refptr.h"
#include "rtc_base/ref_count.h"
namespace webrtc {
class AudioState : public rtc::RefCountInterface { public: struct Config {}; };
}
#endif
