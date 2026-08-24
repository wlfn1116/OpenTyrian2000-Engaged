/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "net_savexfer.h"

#include "config.h"
#include "crashlog.h"
#include "custom_weapon.h"
#include "editship.h"
#include "endless.h"
#include "episodes.h"
#include "font.h"
#include "fonthand.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "net_lobby.h"
#include "network.h"
#include "nortsong.h"
#include "nortvars.h"
#include "opentyr.h"
#include "palette.h"
#include "picload.h"
#include "qa.h"
#include "sndmast.h"
#include "sprite.h"
#include "touch_ui.h"
#include "vga256d.h"
#include "video.h"

#include <stdlib.h>
#include <string.h>

#ifdef WITH_NETWORK

// Separate from the game-session port.
#define SAVE_XFER_PORT   1332

// Keep single-save transfers on version 1. Each bulk choice uses a distinct version.
#define SAVE_XFER_VERSION              1
#define CUSTOM_XFER_TRANSPORT_VERSION  2
#define ALL_XFER_TRANSPORT_VERSION     3
#define SHIPS_XFER_TRANSPORT_VERSION   4
#define WEAPONS_XFER_TRANSPORT_VERSION 5
#define SAVES_XFER_TRANSPORT_VERSION   6

// Fixed-offset, little-endian header. The save record and Endless text follow it.
#define SX_MAGIC         0    /* 4: "OTSV"                                              */
#define SX_VERSION       4    /* 2: SAVE_XFER_VERSION                                   */
#define SX_FLAGS         6    /* 1: bit 0 set for a two-player slot                     */
#define SX_SEAT          7    /* 1: the slot's online seat, 1 or 2                      */
#define SX_ENDLESS_LEN   8    /* 4: Endless record bytes that follow, 0 for none        */
#define SX_RECORD       12    /* SAVE_RECORD_PACKED_SIZE                                */
#define SX_ENDLESS      (SX_RECORD + SAVE_RECORD_PACKED_SIZE)
#define SX_MAX          (SX_ENDLESS + ENDLESS_RUN_WIRE_MAX)

#define SX_FLAG_TWO_PLAYER  0x01

static const Uint8 sx_magic[4] = { 'O', 'T', 'S', 'V' };

// All Saves carries every slot, online seat, and Endless run. High scores remain local.
#define SS_MAGIC       0    /* 4: "OTSA"                    */
#define SS_VERSION     4    /* 2: SAVES_XFER_VERSION         */
#define SS_RESERVED    6    /* 2                             */
#define SS_SAVES_LEN   8    /* 4                             */
#define SS_DATA       12
#define SS_SAVES_MAX  (1024u * 1024u)
#define SS_MAX        (SS_DATA + SS_SAVES_MAX)

#define SAVES_XFER_VERSION 1

static const Uint8 ss_magic[4] = { 'O', 'T', 'S', 'A' };

// Custom Data carries compiled ships, the weapon library, and its enabled flag.
#define CX_MAGIC          0    /* 4: "OTCD"                                      */
#define CX_VERSION        4    /* 2: CUSTOM_XFER_VERSION                         */
#define CX_FLAGS          6    /* 1: bit 0 = custom weapons enabled              */
#define CX_RESERVED       7    /* 1                                              */
#define CX_WEAPONS_LEN    8    /* 4                                              */
#define CX_SHIPS_LEN     12    /* 4                                              */
#define CX_DATA          16
#define CX_MAX           (CX_DATA + CUSTOM_WEAPON_LIBRARY_WIRE_MAX + EXTRA_SHIPS_WIRE_MAX)

#define CUSTOM_XFER_VERSION       2
#define CX_FLAG_WEAPONS_ENABLED   0x01
#define CX_PART_WEAPONS           0x02
#define CX_PART_SHIPS             0x04
#define CX_PART_MASK              (CX_PART_WEAPONS | CX_PART_SHIPS)

static const Uint8 cx_magic[4] = { 'O', 'T', 'C', 'D' };

// Transfer All contains the complete save file and the Custom Data envelope.
// One MiB covers all 22 Endless runs and the remaining save data.
#define AX_MAGIC          0    /* 4: "OTAL"                         */
#define AX_VERSION        4    /* 2: ALL_XFER_VERSION               */
#define AX_RESERVED       6    /* 2                                 */
#define AX_SAVES_LEN      8    /* 4                                 */
#define AX_CUSTOM_LEN    12    /* 4                                 */
#define AX_DATA          16
#define AX_SAVES_MAX     (1024u * 1024u)
#define AX_MAX           (AX_DATA + AX_SAVES_MAX + CX_MAX)

#define ALL_XFER_VERSION 2

static const Uint8 ax_magic[4] = { 'O', 'T', 'A', 'L' };

typedef enum
{
	XFER_SAVE,
	XFER_SAVES,
	XFER_SHIPS,
	XFER_WEAPONS,
	XFER_CUSTOM,
	XFER_ALL,
}
XferKind;

// A generation separates consecutive transfers from stale chunks.
#define SXC_TYPE      0    /* 2 */
#define SXC_VERSION   2    /* 2 */
#define SXC_GEN       4    /* 2 */
#define SXC_CHUNK     6    /* 2 */
#define SXC_COUNT     8    /* 2 */
#define SXC_LEN      10    /* 2 */
#define SXC_HDR      12
#define SXC_PAYLOAD  (NET_PACKET_SIZE - SXC_HDR)

// Fixed-width offer fields keep short or padded packets from shifting data.
#define SXR_TYPE       0    /* 2  */
#define SXR_VERSION    2    /* 2  */
#define SXR_PORT       4    /* 2  */
#define SXR_FLAGS      6    /* 1: SX_FLAG_TWO_PLAYER, plus bit 1 for an Endless run */
#define SXR_EPISODE    7    /* 1  */
#define SXR_LEVEL      8    /* 11 */
#define SXR_SAVE      19    /* 15 */
#define SXR_SENDER    34    /* 11 */
#define SXR_LEN       45

#define SXR_FLAG_ENDLESS  0x02

#define SX_MAX_OFFERS      8
#define SX_SEARCH_MS    1500
#define SX_VOLLEY_MS     400
#define SX_PULL_MS       600    /* retry the request while nothing has arrived */
#define SX_TRANSFER_MS 20000    /* whole-transfer deadline on either side      */

#define SX_XCENTER  (320 / 2)

// Tests measure these proportional-font strings against the 320px menu field.
#define SX_LINE_MAX_PX 300

static const char sxWaitingForDownload[] = "Waiting for a receiver...";
static const char sxUploadKeys        [] = "Enter to custom send, Esc to cancel";
static const char sxSending           [] = "Sending...";
static const char sxThisMachine       [] = "This machine:";
static const char sxSearching         [] = "Searching the local network...";
static const char sxNothingAnswered   [] = "Nothing on this network answered.";
static const char sxDownloadKeys      [] = "Esc to go back";
static const char sxRowTypedAddress   [] = "Enter an address...";
static const char sxRowWaitForSender  [] = "Wait for a sender";
static const char sxAskingAddress     [] = "Asking that address...";
static const char sxDownloading       [] = "Downloading...";
static const char sxWaitingForSender  [] = "Waiting for a sender...";
static const char sxSendHere          [] = "Send to this address.";
static const char sxCancelKey         [] = "Esc to cancel";
static const char sxAnyButton         [] = "Press any button";
static const char sxNoSaveThere       [] = "That address is not sharing a save.";
static const char sxNoSavesThere      [] = "No save slots at that address.";
static const char sxNoShipsThere      [] = "No custom ships at that address.";
static const char sxNoWeaponsThere    [] = "No custom weapons at that address.";
static const char sxNoCustomThere     [] = "No custom data at that address.";
static const char sxNoAllThere        [] = "No complete data at that address.";
static const char sxNeedAddress       [] = "Enter an address.";

static const char *xferUploadTitle(XferKind kind)
{
	switch (kind)
	{
		case XFER_SAVE:    return "Upload Save";
		case XFER_SAVES:   return "Upload All Saves";
		case XFER_SHIPS:   return "Upload Custom Ships";
		case XFER_WEAPONS: return "Upload Custom Weapons";
		case XFER_CUSTOM:  return "Upload Custom Data";
		case XFER_ALL:     return "Upload All Data";
	}
	return "Upload";
}

static const char *xferDownloadTitle(XferKind kind)
{
	switch (kind)
	{
		case XFER_SAVE:    return "Download Save";
		case XFER_SAVES:   return "Download All Saves";
		case XFER_SHIPS:   return "Download Custom Ships";
		case XFER_WEAPONS: return "Download Custom Weapons";
		case XFER_CUSTOM:  return "Download Custom Data";
		case XFER_ALL:     return "Download All Data";
	}
	return "Download";
}

static const char *xferOfferedName(XferKind kind)
{
	switch (kind)
	{
		case XFER_SAVE:    return NULL;
		case XFER_SAVES:   return "All saves";
		case XFER_SHIPS:   return "Custom ships";
		case XFER_WEAPONS: return "Custom weapons";
		case XFER_CUSTOM:  return "Custom ships and weapons";
		case XFER_ALL:     return "All saves and custom data";
	}
	return "Player data";
}

static Uint16 xferPacketType(XferKind kind, Uint16 saveType)
{
	return kind == XFER_SAVE ? saveType
	       : (Uint16)(saveType + (PACKET_CUSTOM_OFFER - PACKET_SAVE_OFFER));
}

static Uint16 xferTransportVersion(XferKind kind)
{
	switch (kind)
	{
		case XFER_SAVE:    return SAVE_XFER_VERSION;
		case XFER_SAVES:   return SAVES_XFER_TRANSPORT_VERSION;
		case XFER_SHIPS:   return SHIPS_XFER_TRANSPORT_VERSION;
		case XFER_WEAPONS: return WEAPONS_XFER_TRANSPORT_VERSION;
		case XFER_CUSTOM:  return CUSTOM_XFER_TRANSPORT_VERSION;
		case XFER_ALL:     return ALL_XFER_TRANSPORT_VERSION;
	}
	return 0;
}

static size_t xferMaxPayload(XferKind kind)
{
	return kind == XFER_SAVE ? SX_MAX : kind == XFER_SAVES ? SS_MAX
	       : kind == XFER_ALL ? AX_MAX : CX_MAX;
}

static bool xferCarriesWeapons(XferKind kind)
{
	return kind == XFER_WEAPONS || kind == XFER_CUSTOM || kind == XFER_ALL;
}

// Weapon transfers need item data and a loaded library before packing or rollback.
static void xferPrepareLocalData(XferKind kind)
{
	if (!xferCarriesWeapons(kind) || customWeaponLibCount >= 1)
		return;
	if (weaponPort[1].name[0] == '\0')
		JE_loadItemDat();
	else
		customWeaponInit();
}

static const char *xferNoOfferLine(XferKind kind)
{
	switch (kind)
	{
		case XFER_SAVE:    return sxNoSaveThere;
		case XFER_SAVES:   return sxNoSavesThere;
		case XFER_SHIPS:   return sxNoShipsThere;
		case XFER_WEAPONS: return sxNoWeaponsThere;
		case XFER_CUSTOM:  return sxNoCustomThere;
		case XFER_ALL:     return sxNoAllThere;
	}
	return sxNoCustomThere;
}

typedef struct
{
	char   address[48];
	Uint16 port;
	char   sender[NET_NAME_MAX + 1];
	char   saveName[15];
	char   levelName[11];
	Uint8  flags;
	Uint8  episode;
}
SaveXferOffer;

// Held until the player chooses a destination slot.
static struct
{
	bool            valid;
	bool            twoPlayer;
	Uint8           seat;
	JE_SaveFileType rec;
	Uint8           endless[ENDLESS_RUN_WIRE_MAX];
	size_t          endlessLen;
}
save_xfer_pending;

// Remember the last typed address for this process.
static char save_xfer_address[64];

bool saveXferAvailable(void) { return true; }

static void saveXferLog(const char *what, const char *detail)
{
	char line[192];
	snprintf(line, sizeof(line), "%s%s%s", what, (detail != NULL && *detail != '\0') ? ": " : "",
	         (detail != NULL) ? detail : "");
	crashlog_netlog_line("save transfer", line);
}

/* Screen */

// The Load Game backdrop does not draw with the fire-red 224..239 palette ramp.
static void saveXferBackdrop(const char *title)
{
	JE_loadPic(VGAScreen2, 2, false);
	draw_font_hv_shadow(VGAScreen2, SX_XCENTER, 20, title, large_font, centered, 15, -3, false, 2);
}

static void saveXferRestore(void)
{
	memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);
}

static void saveXferPresent(void)
{
	mouseCursor = MOUSE_POINTER_NORMAL;
	JE_mouseStart();
	JE_showVGA();
	JE_mouseReplace();
	if (!output_vsync)
		limit_render_fps();
}

static void saveXferPoll(void)
{
	watchdog_heartbeat();
	push_joysticks_as_keyboard();
	service_SDL_events(false);
	saveXferPresent();
}

static bool saveXferWaitForInput(void)
{
	const Uint16 startMouseX = mouse_x;
	const Uint16 startMouseY = mouse_y;

	for (;;)
	{
		push_joysticks_as_keyboard();
		service_SDL_events(false);

		const bool mouseMoved = mouse_x != startMouseX || mouse_y != startMouseY;
		if (newkey || newmouse || mouseMoved)
			return mouseMoved;

		SDL_Delay(1);
	}
}

// Slot names are padded to fourteen characters on disk; draw them without that padding.
static void saveXferTrimName(char *dst, size_t size, const char *src)
{
	while (*src == ' ')
		++src;

	SDL_strlcpy(dst, src, size);

	size_t n = strlen(dst);
	while (n > 0 && dst[n - 1] == ' ')
		dst[--n] = '\0';
}

static void saveXferNotice(const char *title, const char *line1, const char *line2)
{
	saveXferBackdrop(title);
	saveXferRestore();

	draw_font_hv_shadow(VGAScreen, SX_XCENTER, 90, line1, normal_font, centered, 15, -2, false, 2);
	if (line2 != NULL)
		draw_font_hv_shadow(VGAScreen, SX_XCENTER, 110, line2, normal_font, centered, 15, -4, false, 2);
	draw_font_hv_shadow(VGAScreen, SX_XCENTER, 160, sxAnyButton, normal_font, centered, 15, -5, false, 2);

	// Request buttons before the fade and throughout the wait.
	touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
	saveXferPresent();
	fade_palette(colors, 10, 0, 255);

	// Result notices accept only input that begins after the transfer controls are released.
	wait_noinput(true, true, true);
	newkey = newmouse = false;
	touch_ui_consume_input();
	for (;;)
	{
		touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
		if (JE_anyButton())
			break;
		saveXferPoll();
	}

	fade_black(10);
}

/* Payload */

// Return 0 for an empty slot or an encoding failure.
static size_t saveXferPack(Uint8 *out, JE_byte slot)
{
	if (slot < 1 || slot > SAVE_FILES_NUM || saveFiles[slot - 1].level == 0)
		return 0;

	const bool twoPlayer = slot > 11;

	memcpy(&out[SX_MAGIC], sx_magic, sizeof(sx_magic));
	SDLNet_Write16(SAVE_XFER_VERSION, &out[SX_VERSION]);
	out[SX_FLAGS] = (Uint8)(twoPlayer ? SX_FLAG_TWO_PLAYER : 0);
	out[SX_SEAT] = (Uint8)save_slot_online_player(slot);
	save_record_pack(&out[SX_RECORD], &saveFiles[slot - 1]);

	// Never send an Endless save without its run record.
	const size_t endlessLen = endlessSlotSerialize(slot, &out[SX_ENDLESS], ENDLESS_RUN_WIRE_MAX);
	if (endlessLen == 0 && endlessSlotHasRun(slot))
		return 0;

	SDLNet_Write32((Uint32)endlessLen, &out[SX_ENDLESS_LEN]);

	return SX_ENDLESS + endlessLen;
}

static bool saveXferUnpack(const Uint8 *buf, size_t len)
{
	if (buf == NULL || len < SX_ENDLESS)
		return false;
	if (memcmp(&buf[SX_MAGIC], sx_magic, sizeof(sx_magic)) != 0)
		return false;
	if (SDLNet_Read16(&buf[SX_VERSION]) != SAVE_XFER_VERSION)
		return false;

	const Uint32 endlessLen = SDLNet_Read32(&buf[SX_ENDLESS_LEN]);
	if (endlessLen > ENDLESS_RUN_WIRE_MAX || SX_ENDLESS + (size_t)endlessLen != len)
		return false;

	JE_SaveFileType rec;
	save_record_unpack(&rec, &buf[SX_RECORD]);
	if (rec.level == 0)
		return false;   // an empty record is nothing to write

	save_xfer_pending.valid = true;
	save_xfer_pending.twoPlayer = (buf[SX_FLAGS] & SX_FLAG_TWO_PLAYER) != 0;
	save_xfer_pending.seat = (buf[SX_SEAT] == 2) ? 2 : 1;
	save_xfer_pending.rec = rec;
	save_xfer_pending.endlessLen = endlessLen;
	if (endlessLen > 0)
		memcpy(save_xfer_pending.endless, &buf[SX_ENDLESS], endlessLen);

	return true;
}

static size_t savesXferPack(Uint8 *out)
{
	if (out == NULL)
		return 0;

	memcpy(&out[SS_MAGIC], ss_magic, sizeof(ss_magic));
	SDLNet_Write16(SAVES_XFER_VERSION, &out[SS_VERSION]);
	out[SS_RESERVED] = out[SS_RESERVED + 1] = 0;

	const size_t savesLen = save_slots_serialize(&out[SS_DATA], SS_SAVES_MAX);
	if (savesLen == 0 || savesLen > UINT32_MAX)
		return 0;

	SDLNet_Write32((Uint32)savesLen, &out[SS_SAVES_LEN]);
	return SS_DATA + savesLen;
}

static bool savesXferUnpack(const Uint8 *buf, size_t len)
{
	if (buf == NULL || len < SS_DATA || memcmp(&buf[SS_MAGIC], ss_magic, sizeof(ss_magic)) != 0 ||
	    SDLNet_Read16(&buf[SS_VERSION]) != SAVES_XFER_VERSION ||
	    buf[SS_RESERVED] != 0 || buf[SS_RESERVED + 1] != 0)
		return false;

	const size_t savesLen = SDLNet_Read32(&buf[SS_SAVES_LEN]);
	if (savesLen == 0 || savesLen > SS_SAVES_MAX || savesLen != len - SS_DATA)
		return false;

	// Snapshot the slots so a failed adoption cannot leave a partial replacement.
	Uint8 *const oldSaves = malloc(SS_SAVES_MAX);
	const size_t oldSavesLen = oldSaves != NULL
	                             ? save_slots_serialize(oldSaves, SS_SAVES_MAX) : 0;
	if (oldSavesLen == 0)
	{
		free(oldSaves);
		return false;
	}

	const bool adopted = save_slots_adopt(&buf[SS_DATA], savesLen);
	if (!adopted)
		(void)save_slots_adopt(oldSaves, oldSavesLen);
	free(oldSaves);
	return adopted;
}

static size_t customXferPackParts(Uint8 *out, Uint8 parts)
{
	parts &= CX_PART_MASK;
	if (out == NULL || parts == 0)
		return 0;

	memcpy(&out[CX_MAGIC], cx_magic, sizeof(cx_magic));
	SDLNet_Write16(CUSTOM_XFER_VERSION, &out[CX_VERSION]);
	out[CX_FLAGS] = parts;
	if ((parts & CX_PART_WEAPONS) && customWeaponEnabled)
		out[CX_FLAGS] |= CX_FLAG_WEAPONS_ENABLED;
	out[CX_RESERVED] = 0;

	size_t weaponsLen = 0;
	if (parts & CX_PART_WEAPONS)
	{
		weaponsLen = customWeaponSerializeLibrary(&out[CX_DATA],
		                                                CUSTOM_WEAPON_LIBRARY_WIRE_MAX);
		if (weaponsLen == 0 || weaponsLen > UINT32_MAX)
			return 0;
	}

	size_t shipsLen = 0;
	if (parts & CX_PART_SHIPS)
	{
		shipsLen = extraShipsSerializeUser(&out[CX_DATA + weaponsLen], EXTRA_SHIPS_WIRE_MAX);
		if (shipsLen == 0 || shipsLen > UINT32_MAX)
			return 0;
	}

	SDLNet_Write32((Uint32)weaponsLen, &out[CX_WEAPONS_LEN]);
	SDLNet_Write32((Uint32)shipsLen, &out[CX_SHIPS_LEN]);
	return CX_DATA + weaponsLen + shipsLen;
}

static size_t customXferPack(Uint8 *out)
{
	return customXferPackParts(out, CX_PART_MASK);
}

static bool customXferParts(const Uint8 *buf, size_t len, Uint8 expectedParts,
                            const Uint8 **weapons, size_t *weaponsLen,
                            const Uint8 **ships, size_t *shipsLen)
{
	if (buf == NULL || len < CX_DATA || memcmp(&buf[CX_MAGIC], cx_magic, sizeof(cx_magic)) != 0 ||
	    SDLNet_Read16(&buf[CX_VERSION]) != CUSTOM_XFER_VERSION || buf[CX_RESERVED] != 0)
		return false;

	const Uint8 flags = buf[CX_FLAGS];
	const Uint8 parts = flags & CX_PART_MASK;
	if (parts != expectedParts || (flags & ~(CX_FLAG_WEAPONS_ENABLED | CX_PART_MASK)) != 0 ||
	    (!(parts & CX_PART_WEAPONS) && (flags & CX_FLAG_WEAPONS_ENABLED)))
		return false;

	*weaponsLen = SDLNet_Read32(&buf[CX_WEAPONS_LEN]);
	*shipsLen = SDLNet_Read32(&buf[CX_SHIPS_LEN]);
	if (((parts & CX_PART_WEAPONS) ? (*weaponsLen == 0 || *weaponsLen > CUSTOM_WEAPON_LIBRARY_WIRE_MAX)
	                              : *weaponsLen != 0) ||
	    ((parts & CX_PART_SHIPS) ? (*shipsLen == 0 || *shipsLen > EXTRA_SHIPS_WIRE_MAX)
	                            : *shipsLen != 0) ||
	    *weaponsLen > len - CX_DATA || *shipsLen != len - CX_DATA - *weaponsLen)
		return false;

	*weapons = (parts & CX_PART_WEAPONS) ? &buf[CX_DATA] : NULL;
	*ships = (parts & CX_PART_SHIPS) ? &buf[CX_DATA + *weaponsLen] : NULL;
	return !(parts & CX_PART_SHIPS) || extraShipsPayloadValid(*ships, *shipsLen);
}

static bool customXferApply(const Uint8 *buf, size_t len, Uint8 parts)
{
	const Uint8 *weapons, *ships;
	size_t weaponsLen, shipsLen;
	if (!customXferParts(buf, len, parts, &weapons, &weaponsLen, &ships, &shipsLen))
		return false;

	if (parts & CX_PART_WEAPONS)
	{
		if (!customWeaponAdoptLibrary(weapons, weaponsLen))
			return false;
		customWeaponEnabled = (buf[CX_FLAGS] & CX_FLAG_WEAPONS_ENABLED) != 0;
	}
	if ((parts & CX_PART_SHIPS) && !extraShipsAdoptLocal(ships, shipsLen))
		return false;

	// Weapon adoption writes its library and the active design and toggle in opentyrian.cfg.
	return !(parts & CX_PART_WEAPONS) ||
	       (customWeaponLibrarySave() && save_opentyrian_config());
}

static bool customXferUnpackParts(const Uint8 *buf, size_t len, Uint8 parts)
{
	const Uint8 *weapons, *ships;
	size_t weaponsLen, shipsLen;
	if (!customXferParts(buf, len, parts, &weapons, &weaponsLen, &ships, &shipsLen))
		return false;

	Uint8 *const oldCustom = malloc(CX_MAX);
	const size_t oldCustomLen = oldCustom != NULL ? customXferPackParts(oldCustom, parts) : 0;
	if (oldCustomLen == 0)
	{
		free(oldCustom);
		return false;
	}

	const bool adopted = customXferApply(buf, len, parts);
	if (!adopted)
		(void)customXferApply(oldCustom, oldCustomLen, parts);
	free(oldCustom);
	return adopted;
}

static bool customXferUnpack(const Uint8 *buf, size_t len)
{
	return customXferUnpackParts(buf, len, CX_PART_MASK);
}

static size_t allXferPack(Uint8 *out)
{
	if (out == NULL)
		return 0;

	memcpy(&out[AX_MAGIC], ax_magic, sizeof(ax_magic));
	SDLNet_Write16(ALL_XFER_VERSION, &out[AX_VERSION]);
	out[AX_RESERVED] = out[AX_RESERVED + 1] = 0;

	const size_t savesLen = save_file_serialize(&out[AX_DATA], AX_SAVES_MAX);
	if (savesLen == 0 || savesLen > UINT32_MAX)
		return 0;

	const size_t customLen = customXferPack(&out[AX_DATA + savesLen]);
	if (customLen == 0 || customLen > UINT32_MAX)
		return 0;

	SDLNet_Write32((Uint32)savesLen, &out[AX_SAVES_LEN]);
	SDLNet_Write32((Uint32)customLen, &out[AX_CUSTOM_LEN]);
	return AX_DATA + savesLen + customLen;
}

static bool allXferUnpack(const Uint8 *buf, size_t len)
{
	if (buf == NULL || len < AX_DATA || memcmp(&buf[AX_MAGIC], ax_magic, sizeof(ax_magic)) != 0 ||
	    SDLNet_Read16(&buf[AX_VERSION]) != ALL_XFER_VERSION)
		return false;

	const size_t savesLen = SDLNet_Read32(&buf[AX_SAVES_LEN]);
	const size_t customLen = SDLNet_Read32(&buf[AX_CUSTOM_LEN]);
	if (savesLen == 0 || savesLen > AX_SAVES_MAX || customLen == 0 || customLen > CX_MAX ||
	    savesLen > len - AX_DATA || customLen != len - AX_DATA - savesLen)
		return false;

	// Keep a snapshot so Transfer All cannot leave saves and custom data out of sync.
	Uint8 *const oldSaves = malloc(AX_SAVES_MAX);
	const size_t oldSavesLen = oldSaves != NULL ? save_file_serialize(oldSaves, AX_SAVES_MAX) : 0;
	if (oldSavesLen == 0)
	{
		free(oldSaves);
		return false;
	}

	const bool savesAdopted = save_file_adopt(&buf[AX_DATA], savesLen);
	const bool customAdopted = savesAdopted && customXferUnpack(&buf[AX_DATA + savesLen], customLen);
	if (!customAdopted)
		(void)save_file_adopt(oldSaves, oldSavesLen);

	free(oldSaves);
	return customAdopted;
}

static size_t xferPack(XferKind kind, Uint8 *out, JE_byte slot)
{
	switch (kind)
	{
		case XFER_SAVE:    return saveXferPack(out, slot);
		case XFER_SAVES:   return savesXferPack(out);
		case XFER_SHIPS:   return customXferPackParts(out, CX_PART_SHIPS);
		case XFER_WEAPONS: return customXferPackParts(out, CX_PART_WEAPONS);
		case XFER_CUSTOM:  return customXferPack(out);
		case XFER_ALL:     return allXferPack(out);
	}
	return 0;
}

static bool xferUnpack(XferKind kind, const Uint8 *buf, size_t len)
{
	switch (kind)
	{
		case XFER_SAVE:    return saveXferUnpack(buf, len);
		case XFER_SAVES:   return savesXferUnpack(buf, len);
		case XFER_SHIPS:   return customXferUnpackParts(buf, len, CX_PART_SHIPS);
		case XFER_WEAPONS: return customXferUnpackParts(buf, len, CX_PART_WEAPONS);
		case XFER_CUSTOM:  return customXferUnpack(buf, len);
		case XFER_ALL:     return allXferUnpack(buf, len);
	}
	return false;
}

const JE_SaveFileType *saveXferPending(void)
{
	return save_xfer_pending.valid ? &save_xfer_pending.rec : NULL;
}

bool saveXferPendingTwoPlayer(void)
{
	return save_xfer_pending.valid && save_xfer_pending.twoPlayer;
}

void saveXferPendingClear(void)
{
	memset(&save_xfer_pending, 0, sizeof(save_xfer_pending));
}

bool saveXferPendingApply(JE_byte slot, const char *name)
{
	if (!save_xfer_pending.valid || slot < 1 || slot > SAVE_FILES_NUM || name == NULL)
		return false;

	saveFiles[slot - 1] = save_xfer_pending.rec;
	SDL_strlcpy(saveFiles[slot - 1].name, name, sizeof(saveFiles[slot - 1].name));
	save_slot_set_online_player(slot, save_xfer_pending.seat);

	// Do not leave the target slot's old Endless half attached to a Campaign save.
	if (save_xfer_pending.endlessLen == 0
	    || !endlessSlotAdopt(slot, save_xfer_pending.endless, save_xfer_pending.endlessLen))
		endlessSlotClear(slot);

	JE_saveConfiguration();
	saveXferPendingClear();
	return true;
}

/* Socket */

static void saveXferFillReply(XferKind kind, Uint8 *data, JE_byte slot)
{
	memset(data, 0, SXR_LEN);
	SDLNet_Write16(xferPacketType(kind, PACKET_SAVE_REPLY), &data[SXR_TYPE]);
	SDLNet_Write16(xferTransportVersion(kind), &data[SXR_VERSION]);
	SDLNet_Write16(SAVE_XFER_PORT,     &data[SXR_PORT]);

	if (kind == XFER_SAVE)
	{
		data[SXR_FLAGS] = (Uint8)((slot > 11 ? SX_FLAG_TWO_PLAYER : 0)
		                        | (endlessSlotHasRun(slot) ? SXR_FLAG_ENDLESS : 0));
		data[SXR_EPISODE] = saveFiles[slot - 1].episode;
		SDL_strlcpy((char *)&data[SXR_LEVEL], saveFiles[slot - 1].levelName, 11);
		saveXferTrimName((char *)&data[SXR_SAVE], 15, saveFiles[slot - 1].name);
	}
	else
	{
		SDL_strlcpy((char *)&data[SXR_SAVE], xferOfferedName(kind), 15);
	}
	SDL_strlcpy((char *)&data[SXR_SENDER], network_player_name, NET_NAME_MAX + 1);
}

// False reports a local send failure.
static bool saveXferSendTo(UDPsocket sock, UDPpacket *packet, const IPaddress *to, int len)
{
	packet->address = *to;
	packet->len = len;
	return SDLNet_UDP_Send(sock, -1, packet) > 0;
}

// Probe global broadcast and each interface's directed /24.
static bool saveXferProbeVolley(UDPsocket sock, UDPpacket *probe, int len)
{
	IPaddress local[8];
	const int localCount = network_local_addresses(local, (int)COUNTOF(local));

	IPaddress to;
	to.port = SDL_SwapBE16(SAVE_XFER_PORT);

	to.host = 0xffffffffu;   // 255.255.255.255
	bool sent = saveXferSendTo(sock, probe, &to, len);

	for (int i = 0; i < localCount; ++i)
	{
		if (local[i].host == 0)
			continue;

		// `host` is already in network byte order.
		to.host = local[i].host | SDL_SwapBE32(0x000000ffu);
		sent |= saveXferSendTo(sock, probe, &to, len);
	}

	return sent;
}

static bool saveXferIsOurs(const UDPpacket *packet, XferKind kind, Uint16 type, int minLen)
{
	return packet->len >= minLen
	    && SDLNet_Read16(&packet->data[0]) == type
	    && SDLNet_Read16(&packet->data[2]) == xferTransportVersion(kind);
}

/* Upload */

static void saveXferDrawLocalAddresses(int y)
{
	IPaddress addr[8];
	const int count = network_local_addresses(addr, (int)COUNTOF(addr));
	if (count <= 0)
		return;

	draw_font_hv_shadow(VGAScreen, SX_XCENTER, y, sxThisMachine, normal_font, centered, 15, -5, false, 2);

	int shown = 0;
	for (int i = 0; i < count && shown < 3; ++i)
	{
		const Uint32 host = SDL_SwapBE32(addr[i].host);
		if (host == 0)
			continue;

		char line[48];
		snprintf(line, sizeof(line), "%u.%u.%u.%u", (host >> 24) & 0xff, (host >> 16) & 0xff,
		         (host >> 8) & 0xff, host & 0xff);
		draw_font_hv_shadow(VGAScreen, SX_XCENTER, y + 14 + shown * 12, line,
		                    normal_font, centered, 15, -3, false, 2);
		++shown;
	}
}

// Repeat the stream until the receiver acknowledges the complete generation.
static bool saveXferSendPayload(XferKind kind, UDPsocket sock, UDPpacket *out, UDPpacket *in,
                                const IPaddress *to, const Uint8 *payload, size_t total, Uint16 gen,
                                bool *cancelled)
{
	const Uint32 chunks = (Uint32)((total + SXC_PAYLOAD - 1) / SXC_PAYLOAD);
	const Uint32 started = SDL_GetTicks();
	Uint32 sentAt = 0;
	Uint32 nextChunk = 0;
	bool first = true;

	while (SDL_GetTicks() - started < SX_TRANSFER_MS)
	{
		// Poll events and acknowledgements between bursts to keep cancellation responsive.
		if (first || nextChunk != 0 || SDL_GetTicks() - sentAt >= SX_PULL_MS)
		{
			for (int burst = 0; burst < 16; ++burst)
			{
				const Uint32 c = nextChunk;
				const size_t from = (size_t)c * SXC_PAYLOAD;
				const size_t plen = MIN(total - from, (size_t)SXC_PAYLOAD);

				SDLNet_Write16(xferPacketType(kind, PACKET_SAVE_CHUNK), &out->data[SXC_TYPE]);
				SDLNet_Write16(xferTransportVersion(kind), &out->data[SXC_VERSION]);
				SDLNet_Write16(gen,               &out->data[SXC_GEN]);
				SDLNet_Write16((Uint16)c,         &out->data[SXC_CHUNK]);
				SDLNet_Write16((Uint16)chunks,    &out->data[SXC_COUNT]);
				SDLNet_Write16((Uint16)plen,      &out->data[SXC_LEN]);
				memcpy(&out->data[SXC_HDR], payload + from, plen);
				saveXferSendTo(sock, out, to, SXC_HDR + (int)plen);

				if (++nextChunk >= chunks)
				{
					nextChunk = 0;
					sentAt = SDL_GetTicks();
					first = false;
					break;
				}
			}
		}

		while (SDLNet_UDP_Recv(sock, in) > 0)
		{
			if (saveXferIsOurs(in, kind, xferPacketType(kind, PACKET_SAVE_ACK), 6) &&
			    SDLNet_Read16(&in->data[4]) == gen)
				return true;
		}

		saveXferPoll();
		if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
		{
			*cancelled = true;
			return false;
		}

		SDL_Delay(4);
	}

	return false;
}

// `note` is a transient status line under the addresses.
static void saveXferUploadScreen(const char *saveName, const char *note)
{
	saveXferRestore();
	draw_font_hv_shadow(VGAScreen, SX_XCENTER, 60, saveName, normal_font, centered, 15, -1, false, 2);
	draw_font_hv_shadow(VGAScreen, SX_XCENTER, 82, sxWaitingForDownload,
	                    normal_font, centered, 15, -2, false, 2);
	saveXferDrawLocalAddresses(110);
	if (note != NULL)
		draw_font_hv_shadow(VGAScreen, SX_XCENTER, 156, note, normal_font, centered, 15, -3, false, 2);
	draw_font_hv_shadow(VGAScreen, SX_XCENTER, 172, sxUploadKeys,
	                    normal_font, centered, 15, -5, false, 2);
	saveXferPresent();
}

// Push to a receiver that cannot discover or initiate the transfer itself.
static bool saveXferSendToAddress(XferKind kind, UDPsocket sock, UDPpacket *out, UDPpacket *in,
                                  const Uint8 *payload, size_t total, Uint16 gen,
                                  const char *saveName, bool *cancelled)
{
	if (!networkTextEntry(xferUploadTitle(kind), "Send to address:", save_xfer_address,
	                      sizeof(save_xfer_address), networkFilterAddress, false))
	{
		*cancelled = true;
		return false;
	}

	// Ignore a pasted port; transfers always use SAVE_XFER_PORT.
	char host[sizeof(save_xfer_address)];
	SDL_strlcpy(host, save_xfer_address, sizeof(host));
	char *const colon = strrchr(host, ':');
	if (colon != NULL)
		*colon = '\0';

	IPaddress to;
	if (host[0] == '\0' || SDLNet_ResolveHost(&to, host, SAVE_XFER_PORT) == -1)
	{
		saveXferLog("address unusable", host);
		return false;
	}

	saveXferBackdrop(xferUploadTitle(kind));
	saveXferUploadScreen(saveName, sxSending);

	return saveXferSendPayload(kind, sock, out, in, &to, payload, total, gen, cancelled);
}

static void xferUpload(XferKind kind, JE_byte slot)
{
	const char *const title = xferUploadTitle(kind);
	xferPrepareLocalData(kind);
	Uint8 *const payload = malloc(xferMaxPayload(kind));
	const size_t total = payload != NULL ? xferPack(kind, payload, slot) : 0;
	if (total == 0)
	{
		free(payload);
		saveXferNotice(title, kind == XFER_SAVE ? "That save could not be prepared to send."
		                                      : "That data could not be prepared to send.", NULL);
		return;
	}

	if (SDLNet_Init() == -1)
	{
		free(payload);
		saveXferNotice(title, "Networking is unavailable.", NULL);
		return;
	}

	UDPsocket sock = SDLNet_UDP_Open(SAVE_XFER_PORT);
	UDPpacket *const out = sock ? SDLNet_AllocPacket(NET_PACKET_SIZE) : NULL;
	UDPpacket *const in = out ? SDLNet_AllocPacket(NET_PACKET_SIZE) : NULL;
	if (in == NULL)
	{
		if (out) SDLNet_FreePacket(out);
		if (sock) SDLNet_UDP_Close(sock);
		SDLNet_Quit();
		free(payload);
		saveXferNotice(title, "Could not open the transfer port.",
		               "Another copy may already be sharing.");
		return;
	}

	char offeredName[40];
	if (kind == XFER_SAVE)
		saveXferTrimName(offeredName, sizeof(offeredName), saveFiles[slot - 1].name);
	else
		SDL_strlcpy(offeredName, xferOfferedName(kind), sizeof(offeredName));

	saveXferBackdrop(title);
	saveXferUploadScreen(offeredName, NULL);
	fade_palette(colors, 10, 0, 255);

	// Discard the press that opened this screen.
	service_SDL_events(true);

	bool sent = false;
	bool cancelled = false;
	bool push = false;
	Uint16 gen = 0;
	char asked[64];
	asked[0] = '\0';

	while (!sent && !cancelled)
	{
		while (SDLNet_UDP_Recv(sock, in) > 0)
		{
			if (saveXferIsOurs(in, kind, xferPacketType(kind, PACKET_SAVE_OFFER), 4))
			{
				saveXferFillReply(kind, out->data, slot);
				saveXferSendTo(sock, out, &in->address, SXR_LEN);

				// Show that the probe arrived even if the reply cannot get back.
				const Uint32 host = SDL_SwapBE32(in->address.host);
				snprintf(asked, sizeof(asked), "Asked by %u.%u.%u.%u", (host >> 24) & 0xff,
				         (host >> 16) & 0xff, (host >> 8) & 0xff, host & 0xff);
				saveXferUploadScreen(offeredName, asked);
			}
			else if (saveXferIsOurs(in, kind, xferPacketType(kind, PACKET_SAVE_PULL), 4))
			{
				const IPaddress puller = in->address;
				sent = saveXferSendPayload(kind, sock, out, in, &puller, payload, total, ++gen, &cancelled);
				break;
			}
		}

		saveXferPoll();
		if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
			cancelled = true;
		else if (newmouse && lastmouse_but == SDL_BUTTON_RIGHT)
			cancelled = true;
		else if (newkey && (lastkey_scan == SDL_SCANCODE_RETURN
		                    || lastkey_scan == SDL_SCANCODE_KP_ENTER
		                    || lastkey_scan == SDL_SCANCODE_SPACE))
		{
			push = true;
			break;
		}

		SDL_Delay(4);
	}

	if (push)
		sent = saveXferSendToAddress(kind, sock, out, in, payload, total, ++gen, offeredName, &cancelled);

	SDLNet_FreePacket(in);
	SDLNet_FreePacket(out);
	SDLNet_UDP_Close(sock);
	SDLNet_Quit();
	free(payload);

	fade_black(10);

	if (sent)
	{
		JE_playSampleNum(S_SELECT);
		const char *sentLine;
		switch (kind)
		{
			case XFER_SAVE:    sentLine = "The save was sent."; break;
			case XFER_SAVES:   sentLine = "All save slots were sent."; break;
			case XFER_SHIPS:   sentLine = "The custom ships were sent."; break;
			case XFER_WEAPONS: sentLine = "The custom weapons were sent."; break;
			case XFER_CUSTOM:  sentLine = "The custom data was sent."; break;
			case XFER_ALL:     sentLine = "All player data was sent."; break;
			default:           sentLine = "The data was sent."; break;
		}
		saveXferNotice(title, sentLine, NULL);
	}
	else if (!cancelled)
	{
		saveXferNotice(title, "The transfer did not finish.", "Try again from both devices.");
	}
}

void saveXferUpload(JE_byte slot)
{
	xferUpload(XFER_SAVE, slot);
}

void savesXferUpload(void)
{
	xferUpload(XFER_SAVES, 0);
}

void shipsXferUpload(void)
{
	xferUpload(XFER_SHIPS, 0);
}

void weaponsXferUpload(void)
{
	xferUpload(XFER_WEAPONS, 0);
}

void customXferUpload(void)
{
	xferUpload(XFER_CUSTOM, 0);
}

void allXferUpload(void)
{
	xferUpload(XFER_ALL, 0);
}

/* Download */

// Copy fixed-width wire strings into zeroed, bounded fields.
static void saveXferReadReply(const UDPpacket *in, SaveXferOffer *o)
{
	memset(o, 0, sizeof(*o));

	const Uint32 host = SDL_SwapBE32(in->address.host);
	snprintf(o->address, sizeof(o->address), "%u.%u.%u.%u", (host >> 24) & 0xff,
	         (host >> 16) & 0xff, (host >> 8) & 0xff, host & 0xff);

	o->port = SDLNet_Read16(&in->data[SXR_PORT]);
	o->flags = in->data[SXR_FLAGS];
	o->episode = in->data[SXR_EPISODE];
	memcpy(o->levelName, &in->data[SXR_LEVEL], sizeof(o->levelName) - 1);
	memcpy(o->saveName, &in->data[SXR_SAVE], sizeof(o->saveName) - 1);
	memcpy(o->sender, &in->data[SXR_SENDER], sizeof(o->sender) - 1);
}

// Ask one address directly and collect the same reply used by discovery.
static bool saveXferProbeAddress(XferKind kind, UDPsocket sock, UDPpacket *out, UDPpacket *in,
                                 const char *host, SaveXferOffer *result)
{
	IPaddress to;
	if (SDLNet_ResolveHost(&to, host, SAVE_XFER_PORT) == -1)
		return false;

	SDLNet_Write16(xferPacketType(kind, PACKET_SAVE_OFFER), &out->data[0]);
	SDLNet_Write16(xferTransportVersion(kind), &out->data[2]);

	const Uint32 started = SDL_GetTicks();
	Uint32 askedAt = 0;
	bool asked = false;

	while (SDL_GetTicks() - started < SX_SEARCH_MS)
	{
		if (!asked || SDL_GetTicks() - askedAt >= SX_VOLLEY_MS)
		{
			saveXferSendTo(sock, out, &to, 4);
			askedAt = SDL_GetTicks();
			asked = true;
		}

		while (SDLNet_UDP_Recv(sock, in) > 0)
		{
			if (!saveXferIsOurs(in, kind, xferPacketType(kind, PACKET_SAVE_REPLY), SXR_LEN))
				continue;

			saveXferReadReply(in, result);
			return true;
		}

		saveXferPoll();
		SDL_Delay(4);
	}

	return false;
}

static int saveXferFindOffers(XferKind kind, UDPsocket sock, UDPpacket *out, UDPpacket *in,
                              SaveXferOffer *offers)
{
	SDLNet_Write16(xferPacketType(kind, PACKET_SAVE_OFFER), &out->data[0]);
	SDLNet_Write16(xferTransportVersion(kind), &out->data[2]);

	saveXferProbeVolley(sock, out, 4);

	int found = 0;
	const Uint32 started = SDL_GetTicks();
	Uint32 volleyAt = started;

	while (SDL_GetTicks() - started < SX_SEARCH_MS && found < SX_MAX_OFFERS)
	{
		// Retry discovery to tolerate packet loss.
		if (SDL_GetTicks() - volleyAt >= SX_VOLLEY_MS)
		{
			volleyAt = SDL_GetTicks();
			saveXferProbeVolley(sock, out, 4);
		}

		while (SDLNet_UDP_Recv(sock, in) > 0 && found < SX_MAX_OFFERS)
		{
			if (!saveXferIsOurs(in, kind, xferPacketType(kind, PACKET_SAVE_REPLY), SXR_LEN))
				continue;

			SaveXferOffer answered;
			saveXferReadReply(in, &answered);

			bool duplicate = false;
			for (int i = 0; i < found; ++i)
			{
				if (strcmp(offers[i].address, answered.address) == 0)
				{
					duplicate = true;
					break;
				}
			}
			if (duplicate)
				continue;

			offers[found++] = answered;
		}

		saveXferPoll();
		SDL_Delay(4);
	}

	return found;
}

static void saveXferOfferLine(XferKind kind, char *line, size_t size, const SaveXferOffer *o)
{
	if (kind != XFER_SAVE)
	{
		snprintf(line, size, "%s - %s",
		         o->sender[0] != '\0' ? o->sender : o->address, xferOfferedName(kind));
		return;
	}

	char where[32];
	if ((o->flags & SXR_FLAG_ENDLESS) != 0)
		SDL_strlcpy(where, "Endless", sizeof(where));
	else
		snprintf(where, sizeof(where), "Episode %u", o->episode);

	char saveName[sizeof(o->saveName)];
	saveXferTrimName(saveName, sizeof(saveName), o->saveName);

	snprintf(line, size, "%s - %s - %s", o->sender[0] != '\0' ? o->sender : o->address,
	         saveName, where);
}

/* Select a discovered or typed source. The passive row sets `waitForPush`; cancellation and
 * passive mode both return NULL. */
static const SaveXferOffer *saveXferPickOffer(XferKind kind, UDPsocket sock, UDPpacket *out, UDPpacket *in,
                                              SaveXferOffer *offers, int count, bool *waitForPush)
{
	const int typedRow = count;
	const int waitRow = count + 1;
	const int rows = count + 2;

	int selectedIndex = 0;
	int wItem[SX_MAX_OFFERS + 2] = { 0 };
	char status[64];
	status[0] = '\0';

	const int yItems = (count > 0) ? 60 : 96;
	const int dyItems = 18;
	const int hItem = 13;
	const int yGap = (count > 0) ? dyItems : 0;

	for (;;)
	{
		saveXferRestore();

		if (count == 0)
			draw_font_hv_shadow(VGAScreen, SX_XCENTER, 70, sxNothingAnswered,
			                    normal_font, centered, 15, -2, false, 2);

		for (int i = 0; i < rows; ++i)
		{
			char line[96];
			if (i == typedRow)
				SDL_strlcpy(line, sxRowTypedAddress, sizeof(line));
			else if (i == waitRow)
				SDL_strlcpy(line, sxRowWaitForSender, sizeof(line));
			else
				saveXferOfferLine(kind, line, sizeof(line), &offers[i]);

			wItem[i] = JE_textWidth(line, normal_font);
			draw_font_hv_shadow(VGAScreen, SX_XCENTER - wItem[i] / 2,
			                    yItems + dyItems * i + (i >= typedRow ? yGap : 0), line,
			                    normal_font, left_aligned, 15, -4 + (i == selectedIndex ? 2 : 0), false, 2);
		}

		if (status[0] != '\0')
			draw_font_hv_shadow(VGAScreen, SX_XCENTER, 160, status, normal_font, centered, 15, -3, false, 2);

		draw_font_hv_shadow(VGAScreen, SX_XCENTER, 180, sxDownloadKeys,
		                    normal_font, centered, 15, -5, false, 2);

		// Discard the press that started discovery.
		service_SDL_events(true);
		saveXferPresent();

		const bool mouseMoved = saveXferWaitForInput();

		bool action = false;

		if (mouseMoved || newmouse)
		{
			for (int i = 0; i < rows; ++i)
			{
				const int x = SX_XCENTER - wItem[i] / 2;
				const int y = yItems + dyItems * i + (i >= typedRow ? yGap : 0);

				if (mouse_x >= x && mouse_x < x + wItem[i] && mouse_y >= y && mouse_y < y + hItem)
				{
					if (selectedIndex != i)
					{
						JE_playSampleNum(S_CURSOR);
						selectedIndex = i;
					}
					if (newmouse && lastmouse_but == SDL_BUTTON_LEFT)
						action = true;
					break;
				}
			}
		}

		if (newmouse && lastmouse_but == SDL_BUTTON_RIGHT)
			return NULL;

		if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				JE_playSampleNum(S_CURSOR);
				selectedIndex = (selectedIndex == 0) ? rows - 1 : selectedIndex - 1;
				break;
			case SDL_SCANCODE_DOWN:
				JE_playSampleNum(S_CURSOR);
				selectedIndex = (selectedIndex == rows - 1) ? 0 : selectedIndex + 1;
				break;
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
				action = true;
				break;
			case SDL_SCANCODE_ESCAPE:
				JE_playSampleNum(S_SPRING);
				return NULL;
			default:
				break;
			}
		}

		if (!action)
			continue;

		if (selectedIndex == waitRow)
		{
			JE_playSampleNum(S_SELECT);
			*waitForPush = true;
			return NULL;
		}

		if (selectedIndex != typedRow)
		{
			JE_playSampleNum(S_SELECT);
			return &offers[selectedIndex];
		}

		status[0] = '\0';

		if (!networkTextEntry(xferDownloadTitle(kind), "Sender address:", save_xfer_address,
		                      sizeof(save_xfer_address), networkFilterAddress, false))
		{
			continue;
		}

		// Ignore a pasted port; transfers always use SAVE_XFER_PORT.
		char host[sizeof(save_xfer_address)];
		SDL_strlcpy(host, save_xfer_address, sizeof(host));
		char *const colon = strrchr(host, ':');
		if (colon != NULL)
			*colon = '\0';

		if (host[0] == '\0')
		{
			SDL_strlcpy(status, sxNeedAddress, sizeof(status));
			continue;
		}

		saveXferRestore();
		draw_font_hv_shadow(VGAScreen, SX_XCENTER, 100, sxAskingAddress,
		                    normal_font, centered, 15, -2, false, 2);
		saveXferPresent();

		if (saveXferProbeAddress(kind, sock, out, in, host, &offers[typedRow]))
		{
			JE_playSampleNum(S_SELECT);
			return &offers[typedRow];
		}

		JE_playSampleNum(S_CLINK);
		SDL_strlcpy(status, xferNoOfferLine(kind), sizeof(status));
	}
}

/* Pull from `offer`, or wait passively when it is NULL. A passive wait starts its deadline with
 * the first chunk. */
static bool saveXferReceivePayload(XferKind kind, UDPsocket sock, UDPpacket *out, UDPpacket *in,
                                   const SaveXferOffer *offer, bool *cancelled)
{
	IPaddress to;
	const bool asking = offer != NULL;

	if (asking)
	{
		if (SDLNet_ResolveHost(&to, offer->address, offer->port) == -1)
			return false;

		SDLNet_Write16(xferPacketType(kind, PACKET_SAVE_PULL), &out->data[0]);
		SDLNet_Write16(xferTransportVersion(kind), &out->data[2]);
	}

	Uint8 *buf = NULL;
	Uint8 *seen = NULL;
	Uint32 gen = 0, count = 0, have = 0;
	size_t total = 0;
	bool complete = false;
	bool done = false;
	bool anySent = false;

	Uint32 started = SDL_GetTicks();
	Uint32 askedAt = 0;
	bool asked = false;

	while (!complete)
	{
		if ((asking || have > 0) && SDL_GetTicks() - started >= SX_TRANSFER_MS)
			break;

		if (asking && (!asked || (have == 0 && SDL_GetTicks() - askedAt >= SX_PULL_MS)))
		{
			anySent |= saveXferSendTo(sock, out, &to, 4);
			askedAt = SDL_GetTicks();
			asked = true;
		}

		while (SDLNet_UDP_Recv(sock, in) > 0)
		{
			if (!saveXferIsOurs(in, kind, xferPacketType(kind, PACKET_SAVE_CHUNK), SXC_HDR))
				continue;

			const Uint16 packetGen = SDLNet_Read16(&in->data[SXC_GEN]);
			const Uint32 chunk = SDLNet_Read16(&in->data[SXC_CHUNK]);
			const Uint32 chunkCount = SDLNet_Read16(&in->data[SXC_COUNT]);
			const Uint32 plen = SDLNet_Read16(&in->data[SXC_LEN]);

			if (chunkCount == 0 || chunk >= chunkCount || plen > SXC_PAYLOAD
			    || (Uint32)in->len < SXC_HDR + plen
			    || (size_t)chunkCount > (xferMaxPayload(kind) + SXC_PAYLOAD - 1) / SXC_PAYLOAD)
				continue;

			// A new generation replaces any partial stream.
			if (buf == NULL || gen != packetGen || count != chunkCount)
			{
				// The fresh stream owns the deadline and reply address.
				started = SDL_GetTicks();
				to = in->address;

				free(buf);
				free(seen);
				buf = calloc((size_t)chunkCount, SXC_PAYLOAD);
				seen = calloc((size_t)chunkCount, 1);
				if (buf == NULL || seen == NULL)
				{
					free(buf);
					free(seen);
					return false;
				}
				gen = packetGen;
				count = chunkCount;
				have = 0;
				total = 0;
			}

			memcpy(buf + (size_t)chunk * SXC_PAYLOAD, &in->data[SXC_HDR], plen);
			if (chunk == count - 1)
				total = (size_t)chunk * SXC_PAYLOAD + plen;
			if (!seen[chunk])
			{
				seen[chunk] = 1;
				++have;
			}

			if (have >= count && total > 0)
			{
				done = total <= xferMaxPayload(kind) && xferUnpack(kind, buf, total);
				complete = true;

				// Ack a complete invalid payload; resending the same bytes cannot repair it.
				SDLNet_Write16(xferPacketType(kind, PACKET_SAVE_ACK), &in->data[0]);
				SDLNet_Write16(xferTransportVersion(kind), &in->data[2]);
				SDLNet_Write16((Uint16)gen,       &in->data[4]);
				for (int i = 0; i < 3; ++i)
					saveXferSendTo(sock, in, &to, 6);
				break;
			}
		}

		saveXferPoll();
		if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
		{
			*cancelled = true;
			break;
		}

		SDL_Delay(4);
	}

	free(buf);
	free(seen);

	// Distinguish refused sends from an unanswered peer.
	if (!done && asking && !anySent)
		saveXferLog("every request refused", SDLNet_GetError());

	return done;
}

static bool xferDownload(XferKind kind)
{
	const char *const title = xferDownloadTitle(kind);
	xferPrepareLocalData(kind);  // Adoption rollback needs the complete local weapon library.
	if (kind == XFER_SAVE)
		saveXferPendingClear();

	if (SDLNet_Init() == -1)
	{
		saveXferNotice(title, "Networking is unavailable.", NULL);
		return false;
	}

	UDPsocket sock = SDLNet_UDP_Open(0);   // any free port; replies come back to us
	UDPpacket *const out = sock ? SDLNet_AllocPacket(NET_PACKET_SIZE) : NULL;
	UDPpacket *const in = out ? SDLNet_AllocPacket(NET_PACKET_SIZE) : NULL;
	if (in == NULL)
	{
		if (out) SDLNet_FreePacket(out);
		if (sock) SDLNet_UDP_Close(sock);
		SDLNet_Quit();
		saveXferNotice(title, "Could not open a socket.", NULL);
		return false;
	}

	saveXferBackdrop(title);
	saveXferRestore();
	draw_font_hv_shadow(VGAScreen, SX_XCENTER, 90, sxSearching,
	                    normal_font, centered, 15, -2, false, 2);
	saveXferPresent();
	fade_palette(colors, 10, 0, 255);

	SaveXferOffer offers[SX_MAX_OFFERS + 1];
	const int count = saveXferFindOffers(kind, sock, out, in, offers);

	bool got = false;
	bool cancelled = false;
	bool waitForPush = false;
	bool portBusy = false;

	const SaveXferOffer *const pick = saveXferPickOffer(kind, sock, out, in, offers, count, &waitForPush);

	if (pick != NULL)
	{
		saveXferRestore();
		draw_font_hv_shadow(VGAScreen, SX_XCENTER, 100, sxDownloading,
		                    normal_font, centered, 15, -2, false, 2);
		saveXferPresent();

		got = saveXferReceivePayload(kind, sock, out, in, pick, &cancelled);
	}
	else if (waitForPush)
	{
		// Passive transfers need the well-known port instead of the discovery socket.
		SDLNet_UDP_Close(sock);
		sock = SDLNet_UDP_Open(SAVE_XFER_PORT);
		if (sock == NULL)
		{
			portBusy = true;
		}
		else
		{
			saveXferRestore();
			draw_font_hv_shadow(VGAScreen, SX_XCENTER, 60, sxWaitingForSender,
			                    normal_font, centered, 15, -2, false, 2);
			saveXferDrawLocalAddresses(90);
			draw_font_hv_shadow(VGAScreen, SX_XCENTER, 150, sxSendHere,
			                    normal_font, centered, 15, -4, false, 2);
			draw_font_hv_shadow(VGAScreen, SX_XCENTER, 170, sxCancelKey,
			                    normal_font, centered, 15, -5, false, 2);
			saveXferPresent();
			service_SDL_events(true);

			got = saveXferReceivePayload(kind, sock, out, in, NULL, &cancelled);
		}
	}

	fade_black(10);

	saveXferLog(got ? "download finished" : "download did not finish",
	            waitForPush ? "waiting for a sender" : "asking a sender");

	if (portBusy)
		saveXferNotice(title, "Could not open the transfer port.",
		               "Another copy may already be using it.");
	else if ((pick != NULL || waitForPush) && !got && !cancelled)
		saveXferNotice(title, "The transfer did not finish.",
		               "Try again from both devices.");

	SDLNet_FreePacket(in);
	SDLNet_FreePacket(out);
	if (sock != NULL)
		SDLNet_UDP_Close(sock);
	SDLNet_Quit();

	return got;
}

bool saveXferDownload(void)
{
	return xferDownload(XFER_SAVE);
}

bool savesXferDownload(void)
{
	const bool got = xferDownload(XFER_SAVES);
	if (got)
		saveXferNotice("Download All Saves", "All save slots received.", NULL);
	return got;
}

bool shipsXferDownload(void)
{
	const bool got = xferDownload(XFER_SHIPS);
	if (got)
		saveXferNotice("Download Custom Ships", "Custom ships received.", NULL);
	return got;
}

bool weaponsXferDownload(void)
{
	const bool got = xferDownload(XFER_WEAPONS);
	if (got)
		saveXferNotice("Download Custom Weapons", "Custom weapons received.", NULL);
	return got;
}

bool customXferDownload(void)
{
	const bool got = xferDownload(XFER_CUSTOM);
	if (got)
		saveXferNotice("Download Custom Data", "Custom ships and weapons received.", NULL);
	return got;
}

bool allXferDownload(void)
{
	const bool got = xferDownload(XFER_ALL);
	if (got)
		saveXferNotice("Download All Data", "All saves and custom data received.", NULL);
	return got;
}

/* Tests */

void qa_test_save_transfer_preinit(void)
{
	xferPrepareLocalData(XFER_ALL);
	Uint8 *const payload = malloc(AX_MAX);
	const size_t total = payload != NULL ? allXferPack(payload) : 0;
	qa_check(weaponPort[1].name[0] != '\0' && customWeaponLibCount >= 1 && total > AX_DATA,
	         "Transfer All prepares saves and custom data directly from a fresh title screen");
	qa_check(total > AX_DATA && allXferUnpack(payload, total),
	         "Transfer All can adopt data directly on a fresh receiving device");
	free(payload);
}

void qa_test_save_transfer(void)
{
	// The font is proportional, so source length does not prove these lines fit.
	static const char *const centred[] = {
		sxWaitingForDownload, sxUploadKeys, sxSending, sxThisMachine, sxSearching,
		sxNothingAnswered, sxDownloadKeys, sxRowTypedAddress, sxRowWaitForSender,
		sxAskingAddress, sxDownloading, sxWaitingForSender, sxSendHere, sxCancelKey,
		sxAnyButton, sxNoSaveThere, sxNoShipsThere, sxNoWeaponsThere, sxNoCustomThere,
		sxNoSavesThere, sxNoAllThere, sxNeedAddress,
		"Upload All Saves", "Download All Saves", "All saves",
		"Upload Custom Ships", "Download Custom Ships", "Custom ships",
		"Upload Custom Weapons", "Download Custom Weapons", "Custom weapons",
		"Upload Custom Data", "Download Custom Data", "Custom ships and weapons",
		"Upload All Data", "Download All Data", "All saves and custom data",
	};

	for (unsigned i = 0; i < COUNTOF(centred); ++i)
	{
		char label[128];
		snprintf(label, sizeof(label), "the save transfer line '%s' fits the menu field", centred[i]);
		qa_check(JE_textWidth(centred[i], normal_font) <= SX_LINE_MAX_PX, label);
	}

	{
		const unsigned int up = 0x0001u, broadcast = 0x0002u, loopback = 0x0008u,
		                   pointopoint = 0x0010u, running = 0x0040u;
		const unsigned int wifi = up | broadcast | running;

		qa_check(network_interface_carries_lan(wifi),
		         "an up, running, broadcast interface is offered as this machine's address");
		qa_check(!network_interface_carries_lan(wifi | loopback),
		         "...loopback is not, having nobody to reach");
		qa_check(!network_interface_carries_lan((up | running) | pointopoint),
		         "...nor a point-to-point link, which is a tunnel or the cellular interface");
		qa_check(!network_interface_carries_lan(up | broadcast),
		         "...nor an interface that is up but not running");
		qa_check(!network_interface_carries_lan(up | running),
		         "...nor one that carries no broadcast");
	}

	qa_check(SX_ENDLESS == 109 && SXR_LEN == 45 && SXC_HDR == 12,
	         "save-transfer wire offsets retain their protocol widths");
	qa_check(CX_DATA == 16 && xferPacketType(XFER_CUSTOM, PACKET_SAVE_ACK) == PACKET_CUSTOM_ACK,
	         "custom-data transfer retains its envelope and distinct packet family");
	const XferKind bulkKinds[] = { XFER_SAVES, XFER_SHIPS, XFER_WEAPONS, XFER_CUSTOM, XFER_ALL };
	bool bulkKindsSeparated = true;
	for (unsigned i = 0; i < COUNTOF(bulkKinds); ++i)
	{
		bulkKindsSeparated &= xferPacketType(bulkKinds[i], PACKET_SAVE_ACK) == PACKET_CUSTOM_ACK;
		for (unsigned j = i + 1; j < COUNTOF(bulkKinds); ++j)
			bulkKindsSeparated &= xferTransportVersion(bulkKinds[i]) != xferTransportVersion(bulkKinds[j]);
	}
	qa_check(bulkKindsSeparated,
	         "All Saves, Ships, Weapons, Custom Data, and Transfer All cannot negotiate with one another");

	Uint8 *const customPayload = malloc(CX_MAX);
	Uint8 *const shipsPayload = malloc(CX_MAX);
	Uint8 *const weaponsPayload = malloc(CX_MAX);
	const size_t customTotal = customPayload != NULL ? customXferPack(customPayload) : 0;
	const size_t shipsTotal = shipsPayload != NULL
	                        ? customXferPackParts(shipsPayload, CX_PART_SHIPS) : 0;
	const size_t weaponsTotal = weaponsPayload != NULL
	                          ? customXferPackParts(weaponsPayload, CX_PART_WEAPONS) : 0;
	qa_check(customTotal > CX_DATA, "custom ships and the complete weapon library pack together");
	qa_check(shipsTotal > CX_DATA && SDLNet_Read32(&shipsPayload[CX_WEAPONS_LEN]) == 0 &&
	         SDLNet_Read32(&shipsPayload[CX_SHIPS_LEN]) == shipsTotal - CX_DATA,
	         "Custom Ships carries the ship file without a weapon library");
	qa_check(weaponsTotal > CX_DATA && SDLNet_Read32(&weaponsPayload[CX_SHIPS_LEN]) == 0 &&
	         SDLNet_Read32(&weaponsPayload[CX_WEAPONS_LEN]) == weaponsTotal - CX_DATA,
	         "Custom Weapons carries the complete library without a ship file");
	if (customTotal > CX_DATA)
	{
		const size_t weaponsLen = SDLNet_Read32(&customPayload[CX_WEAPONS_LEN]);
		const size_t shipsLen = SDLNet_Read32(&customPayload[CX_SHIPS_LEN]);
		qa_check(weaponsLen > 4 && shipsLen >= 6 + sizeof(JE_ShipsType) &&
		         CX_DATA + weaponsLen + shipsLen == customTotal,
		         "the custom-data envelope delimits both content sets exactly");
		customPayload[CX_MAGIC] ^= 0xff;
		qa_check(!customXferUnpack(customPayload, customTotal),
		         "custom data without the transfer magic is refused before adoption");
		customPayload[CX_MAGIC] ^= 0xff;
	}

	Uint8 *const before = malloc(CUSTOM_WEAPON_LIBRARY_WIRE_MAX);
	Uint8 *const after = malloc(CUSTOM_WEAPON_LIBRARY_WIRE_MAX);
	const size_t beforeLen = before != NULL
	                       ? customWeaponSerializeLibrary(before, CUSTOM_WEAPON_LIBRARY_WIRE_MAX) : 0;
	const bool enabledBefore = customWeaponEnabled;
	const bool shipsAdopted = shipsTotal > CX_DATA &&
	                          customXferUnpackParts(shipsPayload, shipsTotal, CX_PART_SHIPS);
	const size_t afterLen = after != NULL
	                      ? customWeaponSerializeLibrary(after, CUSTOM_WEAPON_LIBRARY_WIRE_MAX) : 0;
	qa_check(shipsAdopted && beforeLen > 0 && beforeLen == afterLen &&
	         memcmp(before, after, beforeLen) == 0 && customWeaponEnabled == enabledBefore,
	         "a Custom Ships transfer leaves custom weapons and their toggle untouched");
	free(after);
	free(before);

	Uint8 *const shipsBefore = malloc(EXTRA_SHIPS_WIRE_MAX);
	Uint8 *const shipsAfter = malloc(EXTRA_SHIPS_WIRE_MAX);
	const size_t shipsBeforeLen = shipsBefore != NULL
	                            ? extraShipsSerializeUser(shipsBefore, EXTRA_SHIPS_WIRE_MAX) : 0;
	const bool weaponsAdopted = weaponsTotal > CX_DATA &&
	                            customXferUnpackParts(weaponsPayload, weaponsTotal, CX_PART_WEAPONS);
	const size_t shipsAfterLen = shipsAfter != NULL
	                           ? extraShipsSerializeUser(shipsAfter, EXTRA_SHIPS_WIRE_MAX) : 0;
	qa_check(weaponsAdopted && shipsBeforeLen > 0 && shipsBeforeLen == shipsAfterLen &&
	         memcmp(shipsBefore, shipsAfter, shipsBeforeLen) == 0,
	         "a Custom Weapons transfer leaves custom ships untouched");
	free(shipsAfter);
	free(shipsBefore);

	free(weaponsPayload);
	free(shipsPayload);
	free(customPayload);

	JE_SaveFileType saved = saveFiles[3 - 1];
	const uint savedSeat = save_slot_online_player(3);

	memset(&saveFiles[3 - 1], 0, sizeof(saveFiles[3 - 1]));
	saveFiles[3 - 1].level = 0x2345;
	saveFiles[3 - 1].score = 5000000000LL;
	saveFiles[3 - 1].episode = 4;
	saveFiles[3 - 1].difficulty = DIFFICULTY_HARD;
	saveFiles[3 - 1].cubes = 9;
	strcpy(saveFiles[3 - 1].levelName, "XFER");
	strcpy(saveFiles[3 - 1].name, "SENDER SAVE");
	for (unsigned i = 0; i < sizeof(saveFiles[3 - 1].items); ++i)
		saveFiles[3 - 1].items[i] = (JE_byte)(i * 3 + 2);

	Uint8 *const payload = malloc(SX_MAX);
	const size_t total = payload != NULL ? saveXferPack(payload, 3) : 0;
	qa_check(total >= SX_ENDLESS, "a save slot packs into a transfer payload");

	saveXferPendingClear();
	qa_check(payload != NULL && saveXferUnpack(payload, total) && saveXferPending() != NULL,
	         "a transfer payload unpacks back into a pending record");
	qa_check(saveXferPending() != NULL
	         && memcmp(saveXferPending(), &saveFiles[3 - 1], sizeof(JE_SaveFileType)) == 0,
	         "the pending record matches the slot that was sent, field for field");
	qa_check(!saveXferPendingTwoPlayer(), "a one-player slot arrives on the one-player page");

	if (payload != NULL)
	{
		saveXferPendingClear();
		payload[SX_MAGIC] ^= 0xff;
		qa_check(!saveXferUnpack(payload, total) && saveXferPending() == NULL,
		         "a payload without the transfer magic is refused");
		payload[SX_MAGIC] ^= 0xff;

		SDLNet_Write16(SAVE_XFER_VERSION + 1, &payload[SX_VERSION]);
		qa_check(!saveXferUnpack(payload, total), "a payload from another wire version is refused");
		SDLNet_Write16(SAVE_XFER_VERSION, &payload[SX_VERSION]);

		SDLNet_Write32(ENDLESS_RUN_WIRE_MAX + 1u, &payload[SX_ENDLESS_LEN]);
		qa_check(!saveXferUnpack(payload, total),
		         "a payload claiming more Endless bytes than it carries is refused");
	}

	free(payload);
	saveXferPendingClear();

	saveFiles[3 - 1] = saved;
	save_slot_set_online_player(3, savedSeat);

	// Single-save and bulk transfers must preserve the saved online seat.
	const JE_byte p2Slot = 12;
	const JE_SaveFileType savedP2 = saveFiles[p2Slot - 1];
	const uint savedP2Seat = save_slot_online_player(p2Slot);
	memset(&saveFiles[p2Slot - 1], 0, sizeof(saveFiles[p2Slot - 1]));
	saveFiles[p2Slot - 1].level = 3;
	saveFiles[p2Slot - 1].episode = 1;
	strcpy(saveFiles[p2Slot - 1].name, "PHONE P2");
	strcpy(saveFiles[p2Slot - 1].levelName, "TYRIAN");
	save_slot_set_online_player(p2Slot, 2);

	Uint8 *const p2Payload = malloc(SX_MAX);
	const size_t p2Total = p2Payload != NULL ? saveXferPack(p2Payload, p2Slot) : 0;
	saveXferPendingClear();
	qa_check(p2Total >= SX_ENDLESS && saveXferUnpack(p2Payload, p2Total) &&
	         save_xfer_pending.twoPlayer && save_xfer_pending.seat == 2,
	         "a single transferred P2 save remains owned by P2 on the receiving device");
	free(p2Payload);
	saveXferPendingClear();

	// All Saves replaces slots in place and preserves the receiver's high scores.
	Uint8 *const originalSlots = malloc(SS_SAVES_MAX);
	const size_t originalSlotsLen = originalSlots != NULL
	                              ? save_slots_serialize(originalSlots, SS_SAVES_MAX) : 0;
	const JE_byte emptySlot = 4;
	memset(&saveFiles[emptySlot - 1], 0, sizeof(saveFiles[emptySlot - 1]));
	endlessSlotClear(emptySlot);
	save_slot_set_online_player(emptySlot, 1);

	Uint8 *const savesPayload = malloc(SS_MAX);
	const size_t savesTotal = savesPayload != NULL ? savesXferPack(savesPayload) : 0;
	qa_check(savesTotal > SS_DATA && SDLNet_Read32(&savesPayload[SS_SAVES_LEN]) == savesTotal - SS_DATA,
	         "All Saves packs every slot and its Endless data in one bounded envelope");

	memset(&saveFiles[p2Slot - 1], 0, sizeof(saveFiles[p2Slot - 1]));
	save_slot_set_online_player(p2Slot, 1);
	memset(&saveFiles[emptySlot - 1], 0, sizeof(saveFiles[emptySlot - 1]));
	saveFiles[emptySlot - 1].level = 99;
	strcpy(saveFiles[emptySlot - 1].name, "MUST CLEAR");

	const T2KHighScoreType originalScore = t2kHighScores[0][0];
	T2KHighScoreType receiverScore = originalScore;
	receiverScore.score = originalScore.score + 12345;
	SDL_strlcpy(receiverScore.playerName, "LOCAL SCORE", sizeof(receiverScore.playerName));
	t2kHighScores[0][0] = receiverScore;

	if (savesTotal > SS_DATA)
	{
		savesPayload[SS_MAGIC] ^= 0xff;
		qa_check(!savesXferUnpack(savesPayload, savesTotal),
		         "All Saves data without its transfer magic is refused before adoption");
		savesPayload[SS_MAGIC] ^= 0xff;
	}
	qa_check(savesTotal > SS_DATA && savesXferUnpack(savesPayload, savesTotal)
	         && save_slot_online_player(p2Slot) == 2
	         && strcmp(saveFiles[p2Slot - 1].name, "PHONE P2") == 0
	         && saveFiles[emptySlot - 1].level == 0
	         && memcmp(&t2kHighScores[0][0], &receiverScore, sizeof(receiverScore)) == 0,
	         "All Saves restores occupied and empty slots in place without replacing high scores");

	free(savesPayload);
	if (originalSlotsLen > 0)
		qa_check(save_slots_adopt(originalSlots, originalSlotsLen),
		         "the All Saves test restores the receiving device's original slot set");
	free(originalSlots);
	t2kHighScores[0][0] = originalScore;

	Uint8 *const allPayload = malloc(AX_MAX);
	const size_t allTotal = allPayload != NULL ? allXferPack(allPayload) : 0;
	qa_check(allTotal > AX_DATA, "Transfer All packs the complete save file and custom-data envelope");
	if (allTotal > AX_DATA)
	{
		const size_t savesLen = SDLNet_Read32(&allPayload[AX_SAVES_LEN]);
		const size_t customLen = SDLNet_Read32(&allPayload[AX_CUSTOM_LEN]);
		qa_check(savesLen > 0 && savesLen <= AX_SAVES_MAX && customLen > CX_DATA &&
		         AX_DATA + savesLen + customLen == allTotal,
		         "the Transfer All envelope delimits saves and custom data exactly");

		save_slot_set_online_player(p2Slot, 1);
		qa_check(allXferUnpack(allPayload, allTotal) && save_slot_online_player(p2Slot) == 2 &&
		         strcmp(saveFiles[p2Slot - 1].name, "PHONE P2") == 0,
		         "Transfer All restores every slot in place, including its P1 or P2 ownership");

		allPayload[AX_MAGIC] ^= 0xff;
		qa_check(!allXferUnpack(allPayload, allTotal),
		         "Transfer All data without its transfer magic is refused before adoption");
		allPayload[AX_MAGIC] ^= 0xff;
	}
	free(allPayload);

	saveFiles[p2Slot - 1] = savedP2;
	save_slot_set_online_player(p2Slot, savedP2Seat);
	JE_saveConfiguration();
}

#else  /* !WITH_NETWORK */

bool saveXferAvailable(void) { return false; }
void saveXferUpload(JE_byte slot) { (void)slot; }
bool saveXferDownload(void) { return false; }
void savesXferUpload(void) { }
bool savesXferDownload(void) { return false; }
void shipsXferUpload(void) { }
bool shipsXferDownload(void) { return false; }
void weaponsXferUpload(void) { }
bool weaponsXferDownload(void) { return false; }
void customXferUpload(void) { }
bool customXferDownload(void) { return false; }
void allXferUpload(void) { }
bool allXferDownload(void) { return false; }
const JE_SaveFileType *saveXferPending(void) { return NULL; }
bool saveXferPendingTwoPlayer(void) { return false; }
bool saveXferPendingApply(JE_byte slot, const char *name) { (void)slot; (void)name; return false; }
void saveXferPendingClear(void) { }
void qa_test_save_transfer_preinit(void) { }
void qa_test_save_transfer(void) { }

#endif
