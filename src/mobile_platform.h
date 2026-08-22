/* Android and iOS platform services. Inert unless a mobile target is defined. */
#ifndef MOBILE_PLATFORM_H
#define MOBILE_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__ANDROID__) || defined(TARGET_IOS)

// Read-only game data. iOS reads it straight out of the app bundle; Android has no
// filesystem view of its assets, so mobile_platform_init() unpacks them into the
// writable directory first and this then points there.
const char *mobile_data_dir(void);

// Writable directory for opentyrian.cfg, saves, and log/. Private to the app on both
// systems, so nothing here survives an uninstall.
const char *mobile_user_dir(void);

// Resolve both directories and, on Android, unpack any data file that is missing or
// stale. Call once at the top of main(), before any file access.
void mobile_platform_init(void);

// Modal single-line text prompt, standing in for the physical keyboard the platform
// lacks. `max_len` 0 uses the output buffer's full capacity, and `numeric` asks for a
// number pad. Returns false on cancel.
bool mobile_swkbd(char *out, size_t out_size, size_t max_len,
                  const char *initial, const char *guide, bool numeric);

// This device's own IPv4 address in network byte order, for the lobby's host line.
// Returns false when the address is unknown; SDL_net's own enumeration works here, so
// this is only a fallback.
bool mobile_get_local_ip(uint32_t *out);

// Native display size in pixels. Either output pointer may be NULL.
void mobile_get_output_size(int *w, int *h);

#endif // __ANDROID__ || TARGET_IOS

#endif // MOBILE_PLATFORM_H
