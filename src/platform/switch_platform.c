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
// was actually brought up.
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

	// Create the standard Switch data directory and this game's writable subdirectory.
	mkdir("sdmc:/switch", 0777);
	mkdir(SWITCH_USER_DIR, 0777);

	// libnx leaves the BSD socket layer unmounted until this call, so without it every socket() in
	// SDL_net fails and netplay is dead.
	if (R_SUCCEEDED(socketInitializeDefault()))
	{
		net_up = true;
		nifmInitialize(NifmServiceType_User);
	}

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

// Vibration handles per npad, No1..No8 then Handheld, rebuilt when the controller style changes.
typedef struct
{
	u32 style;  // the one style tag the handles were made for, 0 for none
	int count;  // a single Joy-Con has one motor, the other styles two
	HidVibrationDeviceHandle handles[2];
}
SwitchRumbleTarget;

static SwitchRumbleTarget rumble_targets[9];

static u32 switch_style_tag(HidNpadIdType id)
{
	static const u32 tags[] = {
		HidNpadStyleTag_NpadFullKey, HidNpadStyleTag_NpadHandheld, HidNpadStyleTag_NpadJoyDual,
		HidNpadStyleTag_NpadJoyLeft, HidNpadStyleTag_NpadJoyRight,
	};
	const u32 set = hidGetNpadStyleSet(id);
	for (size_t i = 0; i < sizeof(tags) / sizeof(*tags); ++i)
		if (set & tags[i])
			return tags[i];
	return 0;
}

static void switch_vibrate(HidNpadIdType id, SwitchRumbleTarget *target, float low, float high)
{
	const u32 style = switch_style_tag(id);
	if (style == 0)
		return;

	if (style != target->style)
	{
		const bool single = style == HidNpadStyleTag_NpadJoyLeft
		                 || style == HidNpadStyleTag_NpadJoyRight;
		target->count = single ? 1 : 2;
		target->style = 0;
		const Result rc = hidInitializeVibrationDevices(target->handles, target->count, id,
		                                                (HidNpadStyleTag)style);
		if (R_FAILED(rc))
			return;
		target->style = style;
	}

	// The motors' default bands, the ones libnx's own idle vibration value uses.
	HidVibrationValue values[2];
	for (int i = 0; i < target->count; ++i)
	{
		values[i].amp_low = low;
		values[i].freq_low = 160.0f;
		values[i].amp_high = high;
		values[i].freq_high = 320.0f;
	}
	hidSendVibrationValues(target->handles, values, target->count);
}

void switch_rumble(int slot, float low, float high)
{
	if (slot < 0 || slot >= 8)
		return;

	if (slot == 0)
		switch_vibrate(HidNpadIdType_Handheld, &rumble_targets[8], low, high);
	switch_vibrate((HidNpadIdType)(HidNpadIdType_No1 + slot), &rumble_targets[slot], low, high);
}

// The style of the controller behind `slot`, handheld first for slot 0; 0 for none.
static u32 switch_slot_style(int slot)
{
	if (slot < 0 || slot >= 8)
		return 0;

	const u32 handheld = slot == 0 ? switch_style_tag(HidNpadIdType_Handheld) : 0;
	return handheld != 0 ? handheld : switch_style_tag((HidNpadIdType)(HidNpadIdType_No1 + slot));
}

bool switch_slot_connected(int slot)
{
	return switch_slot_style(slot) != 0;
}

bool switch_slot_single_joycon(int slot)
{
	const u32 style = switch_slot_style(slot);
	return style == HidNpadStyleTag_NpadJoyLeft || style == HidNpadStyleTag_NpadJoyRight;
}

void switch_controller_setup(int players)
{
	HidLaControllerSupportArg arg;
	hidLaCreateControllerSupportArg(&arg);
	arg.hdr.player_count_min = (s8)players;
	arg.hdr.player_count_max = (s8)players;

	// A cancelled screen keeps whatever controllers were already connected.
	hidLaShowControllerSupport(NULL, &arg);
}

void switch_get_output_size(int *w, int *h)
{
	// Match switch-sdl2's handheld or docked window size from the first frame.
	const bool handheld = appletGetOperationMode() == AppletOperationMode_Handheld;
	if (w != NULL)
		*w = handheld ? 1280 : 1920;
	if (h != NULL)
		*h = handheld ? 720 : 1080;
}

#endif // __SWITCH__
