/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Minimal SDL_net-compatible UDP layer for the PlayStation Vita.
 *
 * VitaSDK has no SDL2_net, so this implements the required UDP and byte-order API on SceNet.
 * It omits TCP and socket sets, supports one address per channel, and keeps SceNet initialized
 * after SDLNet_Quit because the game repeatedly opens short-lived sockets.
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

// This console's addresses, or none when the network is unavailable.
int SDLNet_GetLocalAddresses(IPaddress *addresses, int maxcount);

// `port` is host byte order; 0 asks for any free port. Sockets are opened non-blocking with
// broadcast permitted, matching what SDL_net does.
UDPsocket SDLNet_UDP_Open(Uint16 port);
void SDLNet_UDP_Close(UDPsocket sock);

// Point `channel` at `address` (network byte order). Returns the channel, or -1.
int SDLNet_UDP_Bind(UDPsocket sock, int channel, const IPaddress *address);

// A non-negative channel uses its bound address; -1 uses packet->address.
int SDLNet_UDP_Send(UDPsocket sock, int channel, UDPpacket *packet);

// Return 1 for a packet, 0 when idle, or -1 on error. The packet records its sender and channel.
int SDLNet_UDP_Recv(UDPsocket sock, UDPpacket *packet);

UDPpacket *SDLNet_AllocPacket(int size);
void SDLNet_FreePacket(UDPpacket *packet);

/* Byte-wise helpers support unaligned fields in packed rollback records. */

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
