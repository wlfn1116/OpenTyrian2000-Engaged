/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Nintendo Switch (libnx / devkitA64) platform glue; see switch_platform.h.
 */
#include "switch_platform.h"

#ifdef __SWITCH__

#include <switch.h>

#include <stdlib.h>
#include <sys/stat.h>

// romfsInit() mounts the read-only asset bundle baked into the .nro at romfs:/.
// The SD card (sdmc:/) is mounted automatically by libnx before main() runs.

// Set once socketInitializeDefault() has succeeded, so the exit path only tears down what
// was actually brought up. Stays false in a build without WITH_NETWORK.
static bool net_up = false;

static void switch_platform_exit(void)
{
	if (net_up)
	{
		nifmExit();
		socketExit();
		net_up = false;
	}

	romfsExit();
}

void switch_platform_init(void)
{
	// Mount bundled read-only data. Failure means the SD-card copy is used.
	romfsInit();

	// Make sure the writable config/save directory exists. mkdir() does not create
	// parents, so create sdmc:/switch first; both calls no-op if already present.
	// (sdmc:/switch is the standard homebrew data folder and normally exists.)
	mkdir("sdmc:/switch", 0777);
	mkdir(SWITCH_USER_DIR, 0777);

#ifdef WITH_NETWORK
	// libnx leaves the BSD socket layer unmounted until this call, so without it every
	// socket() in SDL_net fails and netplay is dead. Failure is not fatal: the rest of the
	// game runs fine, and the lobby reports the socket error when the player tries to host
	// or join. nifm is only for reading our own address (see switch_get_local_ip).
	if (R_SUCCEEDED(socketInitializeDefault()))
	{
		net_up = true;
		nifmInitialize(NifmServiceType_User);
	}
#endif

	atexit(switch_platform_exit);
}

bool switch_get_local_ip(uint32_t *out)
{
	if (out == NULL || !net_up)
		return false;

	u32 addr = 0;
	if (R_FAILED(nifmGetCurrentIpAddress(&addr)) || addr == 0)
		return false;

	*out = addr;  // struct in_addr layout: already network byte order
	return true;
}

bool switch_swkbd(char *out, size_t out_size, size_t max_len,
                  const char *initial, const char *guide, bool numeric)
{
	if (out == NULL || out_size == 0)
		return false;

	SwkbdConfig kbd;
	if (R_FAILED(swkbdCreate(&kbd, 0)))
		return false;

	swkbdConfigMakePresetDefault(&kbd);
	if (numeric)
		swkbdConfigSetType(&kbd, SwkbdType_NumPad);
	if (guide != NULL && guide[0] != '\0')
		swkbdConfigSetGuideText(&kbd, guide);
	if (initial != NULL && initial[0] != '\0')
		swkbdConfigSetInitialText(&kbd, initial);

	size_t cap = out_size - 1;
	if (max_len > 0 && max_len < cap)
		cap = max_len;
	swkbdConfigSetStringLenMax(&kbd, (u32)cap);

	Result rc = swkbdShow(&kbd, out, out_size);
	swkbdClose(&kbd);

	return R_SUCCEEDED(rc);
}

void switch_get_output_size(int *w, int *h)
{
	// Mirror the switch-sdl2 video driver: it drives the window to 1280x720 in
	// handheld and 1920x1080 when docked (SWITCH_PumpEvents). Matching that here at
	// window-creation time means we start crisp instead of waiting for the first
	// dock/undock transition to correct the size.
	const bool handheld = appletGetOperationMode() == AppletOperationMode_Handheld;
	if (w != NULL)
		*w = handheld ? 1280 : 1920;
	if (h != NULL)
		*h = handheld ? 720 : 1080;
}

#endif // __SWITCH__
