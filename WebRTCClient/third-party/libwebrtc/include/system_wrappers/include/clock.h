// Minimal stub for system_wrappers/include/clock.h.
// The upstream header was omitted from this prebuilt tree; the only in-tree
// users reference `webrtc::Clock*` as an opaque type in virtual interfaces
// (e.g. NetEqFactory::CreateNetEq). A forward declaration is sufficient for
// the bridge translation units that don't actually implement those factories.
#ifndef SYSTEM_WRAPPERS_INCLUDE_CLOCK_H_
#define SYSTEM_WRAPPERS_INCLUDE_CLOCK_H_

namespace webrtc {
class Clock;
}  // namespace webrtc

#endif  // SYSTEM_WRAPPERS_INCLUDE_CLOCK_H_
