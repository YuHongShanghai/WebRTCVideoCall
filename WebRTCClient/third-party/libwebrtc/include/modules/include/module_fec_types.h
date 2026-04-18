/*
 * Stub header — FecProtectionParams declaration only.
 * The full definition is compiled into libwebrtc.a; we only need the struct
 * declaration to satisfy headers that include this file transitively.
 */
#ifndef MODULES_INCLUDE_MODULE_FEC_TYPES_H_
#define MODULES_INCLUDE_MODULE_FEC_TYPES_H_

namespace webrtc {

struct FecProtectionParams {
  int fec_rate;
  int max_fec_frames;
  int fec_mask_type; // webrtc::FecMaskType enum value
};

}  // namespace webrtc

#endif  // MODULES_INCLUDE_MODULE_FEC_TYPES_H_
