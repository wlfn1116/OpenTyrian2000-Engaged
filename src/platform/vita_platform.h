/* Vita platform services. Inert unless __vita__ is defined. */
#ifndef VITA_PLATFORM_H
#define VITA_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __vita__

// Writable Vita directory shared by config, saves, and user data.
#define VITA_USER_DIR   "ux0:data/opentyrian2000"

// Read-only game data bundled inside the VPK, auto-mounted at app0: by the loader.
#define VITA_DATA_DIR   "app0:data"

// Ensure the ux0: user directory exists and preload the IME sysmodule used for text
// entry. Call once, as early as possible in main() (before any file access).
void vita_platform_init(void);

// Show the modal system keyboard. On cancel, out retains initial.
// max_len == 0 permits out_size - 1 bytes.
bool vita_swkbd(char *out, size_t out_size, size_t max_len,
                const char *initial, const char *guide, bool numeric);

// Return the console's IPv4 address in network byte order after SDLNet_Init.
bool vita_get_local_ip(uint32_t *out);

// Return the fixed 960x544 output size. Either pointer may be NULL.
void vita_get_output_size(int *w, int *h);

#endif // __vita__

#endif // VITA_PLATFORM_H
