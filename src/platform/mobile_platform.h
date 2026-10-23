/* Android and iOS platform services. Inert unless a mobile target is defined. */
#ifndef MOBILE_PLATFORM_H
#define MOBILE_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__ANDROID__) || defined(TARGET_IOS)

// Read-only game data: the iOS bundle or Android's unpacked asset copy.
const char *mobile_data_dir(void);

// App-private writable directory for settings, saves, and logs.
const char *mobile_user_dir(void);

// Resolve the directories and unpack stale Android assets before file access.
void mobile_platform_init(void);

// Modal text prompt. `max_len` 0 uses the whole buffer; `numeric` requests a number pad.
// Returns false on cancel.
bool mobile_swkbd(char *out, size_t out_size, size_t max_len,
                  const char *initial, const char *guide, bool numeric);

// Fallback IPv4 lookup for the lobby host line. Returns false if none is known.
bool mobile_get_local_ip(uint32_t *out);

// Native display size in pixels. Either output pointer may be NULL.
void mobile_get_output_size(int *w, int *h);

#endif // __ANDROID__ || TARGET_IOS

#endif // MOBILE_PLATFORM_H
