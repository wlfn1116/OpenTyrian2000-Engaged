/* Nintendo Switch platform services. Inert unless __SWITCH__ is defined. */
#ifndef SWITCH_PLATFORM_H
#define SWITCH_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __SWITCH__

// Writable data directory on the SD card: holds opentyrian.cfg, save files, and any
// user-supplied data files. Kept in one place so file.c and switch_platform.c agree.
#define SWITCH_USER_DIR   "sdmc:/switch/opentyrian2000"

// Read-only data bundled inside the .nro, mounted by romfsInit().
#define SWITCH_ROMFS_DIR  "romfs:"

// Mount romfs, ensure the SD-card user directory exists, and register cleanup.
// Call once, as early as possible in main() (before any file access).
void switch_platform_init(void);

// Show the modal Switch keyboard. `max_len` 0 uses the output buffer's full
// capacity, and `numeric` selects the number pad. Returns false on cancel.
bool switch_swkbd(char *out, size_t out_size, size_t max_len,
                  const char *initial, const char *guide, bool numeric);

// This console's own IPv4 address, in network byte order (the layout SDL_net's IPaddress.host
// uses).
bool switch_get_local_ip(uint32_t *out);

// Return the native output size: 1280x720 handheld or 1920x1080 docked.
// Either output pointer may be NULL.
void switch_get_output_size(int *w, int *h);

// Vibrate the controllers behind SDL joystick `slot`, following switch-sdl2: slot n is player n+1,
// and slot 0 also covers handheld mode. Motor levels run 0..1; zero stops them. switch-sdl2's own
// rumble drives one motor per controller, never in handheld mode, at a fixed strength.
void switch_rumble(int slot, float low, float high);

// Show the system controller screen for `players` players, where each can register a Joy-Con pair,
// a Pro Controller, or a single Joy-Con held sideways. The screen is skipped when they already are.
void switch_controller_setup(int players);

// Whether SDL joystick `slot` has a controller; switch-sdl2 reports every slot as attached.
bool switch_slot_connected(int slot);
// Whether that controller is a single Joy-Con, which is held sideways.
bool switch_slot_single_joycon(int slot);

#endif // __SWITCH__

#endif // SWITCH_PLATFORM_H
