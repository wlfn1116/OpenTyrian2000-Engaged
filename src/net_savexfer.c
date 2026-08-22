/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "net_savexfer.h"

#include "config.h"
#include "crashlog.h"
#include "endless.h"
#include "font.h"
#include "fonthand.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "network.h"
#include "nortsong.h"
#include "nortvars.h"
#include "opentyr.h"
#include "palette.h"
#include "picload.h"
#include "qa.h"
#include "sndmast.h"
#include "sprite.h"
#include "vga256d.h"
#include "video.h"

#include <stdlib.h>
#include <string.h>

#ifdef WITH_NETWORK

/* Transfers run on their own UDP port so an offered save is never mistaken for a joinable game,
 * and so a machine can host a game and share a save without the two sockets colliding. Open this
 * port alongside the game port when a firewall asks. */
#define SAVE_XFER_PORT   1332

/* Bumped when the payload or any packet below changes meaning. Both sides carry it in every
 * packet and ignore anything that disagrees, so a mixed-version pair finds nothing rather than
 * writing a record it cannot read. */
#define SAVE_XFER_VERSION 1

/* Payload: one packed save record, the slot's Endless half if it has one, and the two facts the
 * receiver cannot derive from the record itself. Fixed offsets, little endian. */
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

/* Chunk header, the shape network.c already uses for custom weapons and Endless runs. The
 * generation retires a stale stream when a receiver comes back for a second transfer. */
#define SXC_TYPE      0    /* 2 */
#define SXC_VERSION   2    /* 2 */
#define SXC_GEN       4    /* 2 */
#define SXC_CHUNK     6    /* 2 */
#define SXC_COUNT     8    /* 2 */
#define SXC_LEN      10    /* 2 */
#define SXC_HDR      12
#define SXC_PAYLOAD  (NET_PACKET_SIZE - SXC_HDR)

/* Offer reply: everything the pick list shows, at fixed offsets so a short or padded packet
 * cannot shift a field. */
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

// The downloaded record, held between the transfer and the slot the player picks for it.
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

bool saveXferAvailable(void) { return true; }

/* --- screen furniture ------------------------------------------------------------------- */

/* The saved-games picture, so a transfer screen reads as part of the menu it was opened from.
 * Its palette leaves the 224..239 ramp fire-red; nothing here draws in that bank. */
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

// Re-present the current frame with a live cursor while a socket loop waits.
static void saveXferPoll(void)
{
	watchdog_heartbeat();
	push_joysticks_as_keyboard();
	service_SDL_events(false);
	saveXferPresent();
}

// Wait for a key, a button, or mouse motion; true means the mouse moved.
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

/* Slot names are stored padded out to fourteen characters. The pad would push a centred line off
 * centre and open a gap in the middle of a list row, so every screen here draws the trimmed name. */
static void saveXferTrimName(char *dst, size_t size, const char *src)
{
	while (*src == ' ')
		++src;

	SDL_strlcpy(dst, src, size);

	size_t n = strlen(dst);
	while (n > 0 && dst[n - 1] == ' ')
		dst[--n] = '\0';
}

// One line of feedback over the backdrop, held until the player acknowledges it.
static void saveXferNotice(const char *title, const char *line1, const char *line2)
{
	saveXferBackdrop(title);
	saveXferRestore();

	draw_font_hv_shadow(VGAScreen, SX_XCENTER, 90, line1, normal_font, centered, 15, -2, false, 2);
	if (line2 != NULL)
		draw_font_hv_shadow(VGAScreen, SX_XCENTER, 110, line2, normal_font, centered, 15, -4, false, 2);
	draw_font_hv_shadow(VGAScreen, SX_XCENTER, 160, "Press any button", normal_font, centered, 15, -5, false, 2);

	saveXferPresent();
	fade_palette(colors, 10, 0, 255);

	wait_noinput(true, true, true);
	while (!JE_anyButton())
		saveXferPoll();

	fade_black(10);
}

/* --- payload --------------------------------------------------------------------------- */

// Pack one slot for the wire. Returns 0 for an empty slot or a record with no room to encode.
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

	/* Serializing returns nothing both for a slot holding no run and for a run that would not
	 * encode. Sending the second as the first would quietly hand over a save with its Endless
	 * half missing, so a slot that has a run and cannot encode it sends nothing at all. */
	const size_t endlessLen = endlessSlotSerialize(slot, &out[SX_ENDLESS], ENDLESS_RUN_WIRE_MAX);
	if (endlessLen == 0 && endlessSlotHasRun(slot))
		return 0;

	SDLNet_Write32((Uint32)endlessLen, &out[SX_ENDLESS_LEN]);

	return SX_ENDLESS + endlessLen;
}

// Validate a received payload and arm it for the slot picker.
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

/* The whole point of the transfer: the record lands byte for byte as it left, so the copy plays
 * out identically. Only the slot it sits in and the name over it are the receiver's. */
bool saveXferPendingApply(JE_byte slot, const char *name)
{
	if (!save_xfer_pending.valid || slot < 1 || slot > SAVE_FILES_NUM || name == NULL)
		return false;

	saveFiles[slot - 1] = save_xfer_pending.rec;
	SDL_strlcpy(saveFiles[slot - 1].name, name, sizeof(saveFiles[slot - 1].name));
	save_slot_set_online_player(slot, save_xfer_pending.seat);

	// A record with no run has to clear whatever run the target slot held, or the two halves
	// of the slot would describe different games.
	if (save_xfer_pending.endlessLen == 0
	    || !endlessSlotAdopt(slot, save_xfer_pending.endless, save_xfer_pending.endlessLen))
		endlessSlotClear(slot);

	JE_saveConfiguration();
	return true;
}

/* --- socket ---------------------------------------------------------------------------- */

// Fill an offer reply describing `slot` for the pick list.
static void saveXferFillReply(Uint8 *data, JE_byte slot)
{
	memset(data, 0, SXR_LEN);
	SDLNet_Write16(PACKET_SAVE_REPLY,  &data[SXR_TYPE]);
	SDLNet_Write16(SAVE_XFER_VERSION,  &data[SXR_VERSION]);
	SDLNet_Write16(SAVE_XFER_PORT,     &data[SXR_PORT]);

	data[SXR_FLAGS] = (Uint8)((slot > 11 ? SX_FLAG_TWO_PLAYER : 0)
	                        | (endlessSlotHasRun(slot) ? SXR_FLAG_ENDLESS : 0));
	data[SXR_EPISODE] = saveFiles[slot - 1].episode;

	SDL_strlcpy((char *)&data[SXR_LEVEL],  saveFiles[slot - 1].levelName, 11);
	saveXferTrimName((char *)&data[SXR_SAVE], 15, saveFiles[slot - 1].name);
	SDL_strlcpy((char *)&data[SXR_SENDER], network_player_name, NET_NAME_MAX + 1);
}

static void saveXferSendTo(UDPsocket sock, UDPpacket *packet, const IPaddress *to, int len)
{
	packet->address = *to;
	packet->len = len;
	// Best effort throughout: a refused broadcast, a downed interface, and an unroutable
	// address all look the same here, and the retries above cover a single loss.
	SDLNet_UDP_Send(sock, -1, packet);
}

// One round of offer probes: global broadcast plus each interface's directed /24.
static void saveXferProbeVolley(UDPsocket sock, UDPpacket *probe, int len)
{
	IPaddress local[8];
	const int localCount = network_local_addresses(local, (int)COUNTOF(local));

	IPaddress to;
	to.port = SDL_SwapBE16(SAVE_XFER_PORT);

	to.host = 0xffffffffu;   // 255.255.255.255
	saveXferSendTo(sock, probe, &to, len);

	for (int i = 0; i < localCount; ++i)
	{
		if (local[i].host == 0)
			continue;

		// Addresses are network byte order, so the host part is the top byte as stored.
		to.host = local[i].host | SDL_SwapBE32(0x000000ffu);
		saveXferSendTo(sock, probe, &to, len);
	}
}

// True when this packet is one of ours and speaks our version. Every minLen covers the header.
static bool saveXferIsOurs(const UDPpacket *packet, Uint16 type, int minLen)
{
	return packet->len >= minLen
	    && SDLNet_Read16(&packet->data[0]) == type
	    && SDLNet_Read16(&packet->data[2]) == SAVE_XFER_VERSION;
}

/* --- upload ---------------------------------------------------------------------------- */

// The addresses to read out to the other player, drawn under the waiting message.
static void saveXferDrawLocalAddresses(int y)
{
	IPaddress addr[8];
	const int count = network_local_addresses(addr, (int)COUNTOF(addr));
	if (count <= 0)
		return;

	draw_font_hv_shadow(VGAScreen, SX_XCENTER, y, "This machine:", normal_font, centered, 15, -5, false, 2);

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

/* Stream the payload to one puller and wait for its acknowledgement. Delivery of the last chunk
 * is not proof the other side assembled the whole thing, so the receiver says so itself. */
static bool saveXferSendPayload(UDPsocket sock, UDPpacket *out, UDPpacket *in,
                                const IPaddress *to, const Uint8 *payload, size_t total, Uint16 gen,
                                bool *cancelled)
{
	const Uint32 chunks = (Uint32)((total + SXC_PAYLOAD - 1) / SXC_PAYLOAD);
	const Uint32 started = SDL_GetTicks();
	Uint32 sentAt = 0;
	bool first = true;

	while (SDL_GetTicks() - started < SX_TRANSFER_MS)
	{
		// Resend the whole stream while the acknowledgement is outstanding: a receiver that
		// lost one chunk is waiting for exactly that chunk to come round again.
		if (first || SDL_GetTicks() - sentAt >= SX_PULL_MS)
		{
			for (Uint32 c = 0; c < chunks; ++c)
			{
				const size_t from = (size_t)c * SXC_PAYLOAD;
				const size_t plen = MIN(total - from, (size_t)SXC_PAYLOAD);

				SDLNet_Write16(PACKET_SAVE_CHUNK, &out->data[SXC_TYPE]);
				SDLNet_Write16(SAVE_XFER_VERSION, &out->data[SXC_VERSION]);
				SDLNet_Write16(gen,               &out->data[SXC_GEN]);
				SDLNet_Write16((Uint16)c,         &out->data[SXC_CHUNK]);
				SDLNet_Write16((Uint16)chunks,    &out->data[SXC_COUNT]);
				SDLNet_Write16((Uint16)plen,      &out->data[SXC_LEN]);
				memcpy(&out->data[SXC_HDR], payload + from, plen);
				saveXferSendTo(sock, out, to, SXC_HDR + (int)plen);
			}
			sentAt = SDL_GetTicks();
			first = false;
		}

		while (SDLNet_UDP_Recv(sock, in) > 0)
		{
			if (saveXferIsOurs(in, PACKET_SAVE_ACK, 6) && SDLNet_Read16(&in->data[4]) == gen)
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

void saveXferUpload(JE_byte slot)
{
	Uint8 *const payload = malloc(SX_MAX);
	const size_t total = payload != NULL ? saveXferPack(payload, slot) : 0;
	if (total == 0)
	{
		free(payload);
		// The caller rejects an empty slot, so reaching here means the record would not encode.
		saveXferNotice("Upload Save", "That save could not be prepared to send.", NULL);
		return;
	}

	if (SDLNet_Init() == -1)
	{
		free(payload);
		saveXferNotice("Upload Save", "Networking is unavailable.", NULL);
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
		// The port is the one thing a second copy of the game on this machine takes away.
		saveXferNotice("Upload Save", "Could not open the transfer port.",
		               "Another copy may already be sharing.");
		return;
	}

	char offeredName[sizeof(saveFiles[0].name)];
	saveXferTrimName(offeredName, sizeof(offeredName), saveFiles[slot - 1].name);

	saveXferBackdrop("Upload Save");
	saveXferRestore();
	draw_font_hv_shadow(VGAScreen, SX_XCENTER, 60, offeredName,
	                    normal_font, centered, 15, -1, false, 2);
	draw_font_hv_shadow(VGAScreen, SX_XCENTER, 82, "Waiting for another device to download...",
	                    normal_font, centered, 15, -2, false, 2);
	saveXferDrawLocalAddresses(110);
	draw_font_hv_shadow(VGAScreen, SX_XCENTER, 170, "Esc to cancel", normal_font, centered, 15, -5, false, 2);
	saveXferPresent();
	fade_palette(colors, 10, 0, 255);

	// Whatever opened this screen is still latched; a fresh press is what cancels it.
	service_SDL_events(true);

	bool sent = false;
	bool cancelled = false;
	Uint16 gen = 0;

	while (!sent && !cancelled)
	{
		while (SDLNet_UDP_Recv(sock, in) > 0)
		{
			if (saveXferIsOurs(in, PACKET_SAVE_OFFER, 4))
			{
				saveXferFillReply(out->data, slot);
				saveXferSendTo(sock, out, &in->address, SXR_LEN);
			}
			else if (saveXferIsOurs(in, PACKET_SAVE_PULL, 4))
			{
				const IPaddress puller = in->address;
				sent = saveXferSendPayload(sock, out, in, &puller, payload, total, ++gen, &cancelled);
				break;
			}
		}

		saveXferPoll();
		if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
			cancelled = true;
		else if (newmouse && lastmouse_but == SDL_BUTTON_RIGHT)
			cancelled = true;

		SDL_Delay(4);
	}

	SDLNet_FreePacket(in);
	SDLNet_FreePacket(out);
	SDLNet_UDP_Close(sock);
	SDLNet_Quit();
	free(payload);

	fade_black(10);

	if (sent)
	{
		JE_playSampleNum(S_SELECT);
		saveXferNotice("Upload Save", "The save was sent.", NULL);
	}
	else if (!cancelled)
	{
		saveXferNotice("Upload Save", "The transfer did not finish.", "Try again from both devices.");
	}
}

/* --- download -------------------------------------------------------------------------- */

// Probe the network and collect what answers. Returns how many distinct machines replied.
static int saveXferFindOffers(UDPsocket sock, UDPpacket *out, UDPpacket *in, SaveXferOffer *offers)
{
	SDLNet_Write16(PACKET_SAVE_OFFER, &out->data[0]);
	SDLNet_Write16(SAVE_XFER_VERSION, &out->data[2]);

	saveXferProbeVolley(sock, out, 4);

	int found = 0;
	const Uint32 started = SDL_GetTicks();
	Uint32 volleyAt = started;

	while (SDL_GetTicks() - started < SX_SEARCH_MS && found < SX_MAX_OFFERS)
	{
		// A probe is one datagram; repeat the round so a single loss cannot empty the list.
		if (SDL_GetTicks() - volleyAt >= SX_VOLLEY_MS)
		{
			volleyAt = SDL_GetTicks();
			saveXferProbeVolley(sock, out, 4);
		}

		while (SDLNet_UDP_Recv(sock, in) > 0 && found < SX_MAX_OFFERS)
		{
			if (!saveXferIsOurs(in, PACKET_SAVE_REPLY, SXR_LEN))
				continue;

			const Uint32 host = SDL_SwapBE32(in->address.host);
			char address[48];
			snprintf(address, sizeof(address), "%u.%u.%u.%u", (host >> 24) & 0xff,
			         (host >> 16) & 0xff, (host >> 8) & 0xff, host & 0xff);

			// One machine answers once per probe we sent it, so drop the repeats.
			bool duplicate = false;
			for (int i = 0; i < found; ++i)
			{
				if (strcmp(offers[i].address, address) == 0)
				{
					duplicate = true;
					break;
				}
			}
			if (duplicate)
				continue;

			SaveXferOffer *const o = &offers[found++];
			memset(o, 0, sizeof(*o));
			SDL_strlcpy(o->address, address, sizeof(o->address));
			o->port = SDLNet_Read16(&in->data[SXR_PORT]);
			o->flags = in->data[SXR_FLAGS];
			o->episode = in->data[SXR_EPISODE];
			// Fixed-width fields from another machine; copy a bounded run and terminate here.
			memcpy(o->levelName, &in->data[SXR_LEVEL], sizeof(o->levelName) - 1);
			memcpy(o->saveName, &in->data[SXR_SAVE], sizeof(o->saveName) - 1);
			memcpy(o->sender, &in->data[SXR_SENDER], sizeof(o->sender) - 1);
		}

		saveXferPoll();
		SDL_Delay(4);
	}

	return found;
}

// One row of the pick list: who is sharing, what it is called, and where it left off.
static void saveXferOfferLine(char *line, size_t size, const SaveXferOffer *o)
{
	char where[32];
	if ((o->flags & SXR_FLAG_ENDLESS) != 0)
		SDL_strlcpy(where, "Endless", sizeof(where));
	else
		snprintf(where, sizeof(where), "Episode %u", o->episode);

	// The name arrives trimmed, but it came off another machine, so trim it here as well.
	char saveName[sizeof(o->saveName)];
	saveXferTrimName(saveName, sizeof(saveName), o->saveName);

	snprintf(line, size, "%s - %s - %s", o->sender[0] != '\0' ? o->sender : o->address,
	         saveName, where);
}

// Let the player choose which machine to pull from. Returns NULL if they backed out.
static const SaveXferOffer *saveXferPickOffer(const SaveXferOffer *offers, int count)
{
	int selectedIndex = 0;
	int wItem[SX_MAX_OFFERS] = { 0 };

	const int yItems = 60;
	const int dyItems = 18;
	const int hItem = 13;

	for (;;)
	{
		saveXferRestore();

		for (int i = 0; i < count; ++i)
		{
			char line[96];
			saveXferOfferLine(line, sizeof(line), &offers[i]);

			wItem[i] = JE_textWidth(line, normal_font);
			draw_font_hv_shadow(VGAScreen, SX_XCENTER - wItem[i] / 2, yItems + dyItems * i, line,
			                    normal_font, left_aligned, 15, -4 + (i == selectedIndex ? 2 : 0), false, 2);
		}

		draw_font_hv_shadow(VGAScreen, SX_XCENTER, 180, "Enter to download, Esc to go back",
		                    normal_font, centered, 15, -5, false, 2);

		// Drop whatever opened this screen: the press that started the search stays latched
		// through the whole discovery window and would read here as an instant pick.
		service_SDL_events(true);
		saveXferPresent();

		const bool mouseMoved = saveXferWaitForInput();

		bool action = false;

		if (mouseMoved || newmouse)
		{
			for (int i = 0; i < count; ++i)
			{
				const int x = SX_XCENTER - wItem[i] / 2;
				const int y = yItems + dyItems * i;

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
				selectedIndex = (selectedIndex == 0) ? count - 1 : selectedIndex - 1;
				break;
			case SDL_SCANCODE_DOWN:
				JE_playSampleNum(S_CURSOR);
				selectedIndex = (selectedIndex == count - 1) ? 0 : selectedIndex + 1;
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

		if (action)
		{
			JE_playSampleNum(S_SELECT);
			return &offers[selectedIndex];
		}
	}
}

// Ask one machine for its save and assemble the chunks it sends back.
static bool saveXferReceivePayload(UDPsocket sock, UDPpacket *out, UDPpacket *in,
                                   const SaveXferOffer *offer, bool *cancelled)
{
	IPaddress to;
	if (SDLNet_ResolveHost(&to, offer->address, offer->port) == -1)
		return false;

	SDLNet_Write16(PACKET_SAVE_PULL,  &out->data[0]);
	SDLNet_Write16(SAVE_XFER_VERSION, &out->data[2]);

	Uint8 *buf = NULL;
	Uint8 *seen = NULL;
	Uint32 gen = 0, count = 0, have = 0;
	size_t total = 0;
	bool done = false;

	const Uint32 started = SDL_GetTicks();
	Uint32 askedAt = 0;
	bool asked = false;

	while (!done && SDL_GetTicks() - started < SX_TRANSFER_MS)
	{
		if (!asked || (have == 0 && SDL_GetTicks() - askedAt >= SX_PULL_MS))
		{
			saveXferSendTo(sock, out, &to, 4);
			askedAt = SDL_GetTicks();
			asked = true;
		}

		while (SDLNet_UDP_Recv(sock, in) > 0)
		{
			if (!saveXferIsOurs(in, PACKET_SAVE_CHUNK, SXC_HDR))
				continue;

			const Uint16 packetGen = SDLNet_Read16(&in->data[SXC_GEN]);
			const Uint32 chunk = SDLNet_Read16(&in->data[SXC_CHUNK]);
			const Uint32 chunkCount = SDLNet_Read16(&in->data[SXC_COUNT]);
			const Uint32 plen = SDLNet_Read16(&in->data[SXC_LEN]);

			if (chunkCount == 0 || chunk >= chunkCount || plen > SXC_PAYLOAD
			    || (Uint32)in->len < SXC_HDR + plen
			    || (size_t)chunkCount * SXC_PAYLOAD > SX_MAX)
				continue;

			// A new generation is a fresh stream; whatever was half-assembled is stale.
			if (buf == NULL || gen != packetGen || count != chunkCount)
			{
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
				done = saveXferUnpack(buf, total);

				// Answer either way: a payload we could not use is still one we received, and
				// the sender resends the whole stream until something answers.
				SDLNet_Write16(PACKET_SAVE_ACK,   &in->data[0]);
				SDLNet_Write16(SAVE_XFER_VERSION, &in->data[2]);
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
	return done;
}

bool saveXferDownload(void)
{
	saveXferPendingClear();

	if (SDLNet_Init() == -1)
	{
		saveXferNotice("Download Save", "Networking is unavailable.", NULL);
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
		saveXferNotice("Download Save", "Could not open a socket.", NULL);
		return false;
	}

	// Draw the searching frame first: the probe runs for its whole window and is re-presented
	// from saveXferPoll while it does.
	saveXferBackdrop("Download Save");
	saveXferRestore();
	draw_font_hv_shadow(VGAScreen, SX_XCENTER, 90, "Searching the local network...",
	                    normal_font, centered, 15, -2, false, 2);
	saveXferPresent();
	fade_palette(colors, 10, 0, 255);

	SaveXferOffer offers[SX_MAX_OFFERS];
	const int count = saveXferFindOffers(sock, out, in, offers);

	bool got = false;
	bool cancelled = false;

	if (count == 0)
	{
		fade_black(10);
		saveXferNotice("Download Save", "No device is sharing a save.",
		               "Start Upload Save on the other device.");
	}
	else
	{
		const SaveXferOffer *const pick = saveXferPickOffer(offers, count);
		if (pick != NULL)
		{
			saveXferRestore();
			draw_font_hv_shadow(VGAScreen, SX_XCENTER, 90, "Downloading...",
			                    normal_font, centered, 15, -2, false, 2);
			saveXferPresent();

			got = saveXferReceivePayload(sock, out, in, pick, &cancelled);
		}

		fade_black(10);

		// Backing out is a decision, not a fault; only a real failure is worth a notice.
		if (pick != NULL && !got && !cancelled)
			saveXferNotice("Download Save", "The transfer did not finish.",
			               "Try again from both devices.");
	}

	SDLNet_FreePacket(in);
	SDLNet_FreePacket(out);
	SDLNet_UDP_Close(sock);
	SDLNet_Quit();

	return got;
}

/* --- tests ----------------------------------------------------------------------------- */

/* The payload codec, exercised without a socket: a record survives the round trip whole, and the
 * three ways a hostile or mismatched payload can arrive are all refused. */
void qa_test_save_transfer(void)
{
	qa_check(SX_ENDLESS == 109 && SXR_LEN == 45 && SXC_HDR == 12,
	         "save-transfer wire offsets retain their protocol widths");

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
}

#else  /* !WITH_NETWORK */

bool saveXferAvailable(void) { return false; }
void saveXferUpload(JE_byte slot) { (void)slot; }
bool saveXferDownload(void) { return false; }
const JE_SaveFileType *saveXferPending(void) { return NULL; }
bool saveXferPendingTwoPlayer(void) { return false; }
bool saveXferPendingApply(JE_byte slot, const char *name) { (void)slot; (void)name; return false; }
void saveXferPendingClear(void) { }
void qa_test_save_transfer(void) { }

#endif
