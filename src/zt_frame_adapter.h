#pragma once

#include <stdint.h>

// Optional extension implemented by the patched libzt build. The stock
// ZeroTierSockets API does not export these symbols; zt-globald treats their
// absence as a degraded (control-plane only) mode.
typedef void (*zt_frame_callback)(uint64_t network_id, const void* frame, unsigned int length);

#if defined(__GNUC__)
#define ZT_WEAK __attribute__((weak))
#else
#define ZT_WEAK
#endif

extern "C" {
int zts_net_set_frame_callback(uint64_t network_id, zt_frame_callback callback) ZT_WEAK;
int zts_net_send_frame(uint64_t network_id, const void* frame, unsigned int length) ZT_WEAK;
}
