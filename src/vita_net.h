/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Minimal SDL_net-compatible UDP layer for the PlayStation Vita.
 *
 * VitaSDK ships no SDL2_net package and no BSD socket wrappers in libc, so the netplay code
 * has nothing to link against there. Rather than vendor the whole library, this reimplements
 * exactly the slice the game uses -- UDP sockets and the byte-order helpers -- on top of
 * SceNet. network.h picks this header instead of <SDL_net.h> on the Vita, so no call site
 * changes.
 *
 * Deliberate simplifications versus real SDL_net (none of them reachable from this game):
 *   - UDP only; there is no TCP or SDLNet_CheckSockets/socket-set support.
 *   - One bound address per channel, not SDLNET_MAX_UDPADDRESSES of them.
 *   - SDLNet_Quit only drops the reference count. The SceNet stack, once up, stays up for
 *     the life of the process: sceNetInit owns a memory pool that would have to be torn
 *     down and rebuilt, and the game opens and closes sockets repeatedly (every lobby
 *     probe does an Init/Quit pair).
 */
#ifndef VITA_NET_H
#define VITA_NET_H

#if defined(__vita__) && defined(WITH_NETWORK)

#include "SDL.h"

// IPaddress keeps both fields in network byte order, exactly like SDL_net.
typedef struct
{
	Uint32 host;
	Uint16 port;
}
IPaddress;

typedef struct UDPsocket_s *UDPsocket;

typedef struct
{
	int channel;        // src/dst channel of the packet, or -1
	Uint8 *data;
	int len;
	int maxlen;
	int status;         // bytes sent/received, or the negative error
	IPaddress address;
}
UDPpacket;

#define SDLNET_MAX_UDPCHANNELS  32

int SDLNet_Init(void);
void SDLNet_Quit(void);
const char *SDLNet_GetError(void);

// Fills in `address` from a dotted quad or a hostname (`port` is host byte order).
// Returns 0, or -1 if the name could not be resolved.
int SDLNet_ResolveHost(IPaddress *address, const char *host, Uint16 port);

// This console's addresses -- in practice the one address SceNetCtl reports, or none if the
// network is down. Returns how many were written.
int SDLNet_GetLocalAddresses(IPaddress *addresses, int maxcount);

// `port` is host byte order; 0 asks for any free port. Sockets are opened non-blocking with
// broadcast permitted, matching what SDL_net does.
UDPsocket SDLNet_UDP_Open(Uint16 port);
void SDLNet_UDP_Close(UDPsocket sock);

// Point `channel` at `address` (network byte order). Returns the channel, or -1.
int SDLNet_UDP_Bind(UDPsocket sock, int channel, const IPaddress *address);

// channel >= 0 sends to that channel's bound address and ignores packet->address;
// channel -1 sends to packet->address. Returns the number of destinations sent to, so
// 0 means failure.
int SDLNet_UDP_Send(UDPsocket sock, int channel, UDPpacket *packet);

// Returns 1 if a packet was received, 0 if none was waiting, -1 on error. On success
// packet->address is the sender and packet->channel is the channel it is bound to (-1 if
// it is bound to none).
int SDLNet_UDP_Recv(UDPsocket sock, UDPpacket *packet);

UDPpacket *SDLNet_AllocPacket(int size);
void SDLNet_FreePacket(UDPpacket *packet);

/* Byte-order helpers. Written a byte at a time rather than as aligned casts: the rollback
 * input packets pack 14-byte records, so a 16- or 32-bit field lands on an odd offset. */

static inline void SDLNet_Write16(Uint16 value, void *areap)
{
	Uint8 *area = (Uint8 *)areap;
	area[0] = (Uint8)(value >> 8);
	area[1] = (Uint8)value;
}

static inline void SDLNet_Write32(Uint32 value, void *areap)
{
	Uint8 *area = (Uint8 *)areap;
	area[0] = (Uint8)(value >> 24);
	area[1] = (Uint8)(value >> 16);
	area[2] = (Uint8)(value >> 8);
	area[3] = (Uint8)value;
}

static inline Uint16 SDLNet_Read16(const void *areap)
{
	const Uint8 *area = (const Uint8 *)areap;
	return (Uint16)(((Uint16)area[0] << 8) | area[1]);
}

static inline Uint32 SDLNet_Read32(const void *areap)
{
	const Uint8 *area = (const Uint8 *)areap;
	return ((Uint32)area[0] << 24) | ((Uint32)area[1] << 16) | ((Uint32)area[2] << 8) | area[3];
}

#endif // __vita__ && WITH_NETWORK

#endif // VITA_NET_H
