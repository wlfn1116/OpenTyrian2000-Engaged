/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Minimal SDL_net-compatible UDP API for VitaSDK. It supports one address per channel and leaves
 * SceNet ready for short-lived game sockets.
 */
#ifndef VITA_NET_H
#define VITA_NET_H

#ifdef __vita__

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

IPaddress *SDLNet_UDP_GetPeerAddress(UDPsocket sock, int channel);

// A non-negative channel uses its bound address; -1 uses packet->address.
int SDLNet_UDP_Send(UDPsocket sock, int channel, UDPpacket *packet);

// Return 1 for a packet, 0 when idle, or -1 on error. The packet records its sender and channel.
int SDLNet_UDP_Recv(UDPsocket sock, UDPpacket *packet);

UDPpacket *SDLNet_AllocPacket(int size);
void SDLNet_FreePacket(UDPpacket *packet);

#endif // __vita__

#endif // VITA_NET_H
