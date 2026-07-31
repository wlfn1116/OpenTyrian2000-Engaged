/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * SceNet backing for the SDL_net subset the netplay code uses -- see vita_net.h.
 */
#include "vita_net.h"

#if defined(__vita__) && defined(WITH_NETWORK)

#include "vita_platform.h"

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

#include <stdlib.h>
#include <string.h>

// SceNet wants a memory pool it keeps for its own lifetime. 1 MiB is generous for the two
// UDP sockets this game ever has open at once, and it is never freed: see the header note
// on why the stack stays up once raised.
#define VITA_NET_POOL_SIZE  (1 * 1024 * 1024)

static int net_refcount = 0;
static bool net_stack_up = false;
static void *net_pool = NULL;

struct UDPsocket_s
{
	int fd;
	IPaddress channel_addr[SDLNET_MAX_UDPCHANNELS];
	bool channel_bound[SDLNET_MAX_UDPCHANNELS];
};

// SceNet reports failure by returning the error code itself, negative-when-signed, rather
// than by setting errno.
static bool sce_failed(int ret)
{
	return ret < 0;
}

static bool sce_would_block(int ret)
{
	return ret < 0 && (unsigned int)ret == SCE_NET_ERROR_EWOULDBLOCK;
}

static void fill_sockaddr(SceNetSockaddrIn *sa, Uint32 host_be, Uint16 port_be)
{
	memset(sa, 0, sizeof(*sa));
	sa->sin_len = sizeof(*sa);
	sa->sin_family = SCE_NET_AF_INET;
	sa->sin_port = port_be;
	sa->sin_addr.s_addr = host_be;
}

// Raise the SceNet stack. Once up it stays up; only the first caller does any work.
static bool net_stack_init(void)
{
	if (net_stack_up)
		return true;

	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);

	net_pool = malloc(VITA_NET_POOL_SIZE);
	if (net_pool == NULL)
	{
		SDL_SetError("out of memory allocating the SceNet pool");
		return false;
	}

	SceNetInitParam param;
	param.memory = net_pool;
	param.size = VITA_NET_POOL_SIZE;
	param.flags = 0;

	const int ret = sceNetInit(&param);
	if (sce_failed(ret) && (unsigned int)ret != SCE_NET_ERROR_EBUSY)
	{
		free(net_pool);
		net_pool = NULL;
		SDL_SetError("sceNetInit failed (0x%08X)", (unsigned int)ret);
		return false;
	}

	// Only needed to read our own address back out; a failure here costs the "This machine:"
	// readout in the lobby and nothing else, so it is not checked.
	sceNetCtlInit();

	net_stack_up = true;
	return true;
}

int SDLNet_Init(void)
{
	if (net_refcount == 0 && !net_stack_init())
		return -1;

	++net_refcount;
	return 0;
}

void SDLNet_Quit(void)
{
	if (net_refcount > 0)
		--net_refcount;
}

const char *SDLNet_GetError(void)
{
	return SDL_GetError();
}

int SDLNet_ResolveHost(IPaddress *address, const char *host, Uint16 port)
{
	if (address == NULL)
		return -1;

	address->port = SDL_SwapBE16(port);

	if (host == NULL)
	{
		address->host = 0;  // INADDR_ANY
		return 0;
	}

	SceNetInAddr addr;
	if (sceNetInetPton(SCE_NET_AF_INET, host, &addr) == 1)
	{
		address->host = addr.s_addr;
		return 0;
	}

	// Not a dotted quad, so treat it as a name. This blocks, but so does the join it is
	// part of, and the lobby has already stopped drawing by then.
	const int rid = sceNetResolverCreate("opentyrian", NULL, 0);
	if (rid < 0)
	{
		SDL_SetError("sceNetResolverCreate failed (0x%08X)", (unsigned int)rid);
		return -1;
	}

	const int ret = sceNetResolverStartNtoa(rid, host, &addr, 3 * 1000 * 1000, 1, 0);
	sceNetResolverDestroy(rid);

	if (sce_failed(ret))
	{
		SDL_SetError("could not resolve %s", host);
		return -1;
	}

	address->host = addr.s_addr;
	return 0;
}

bool vita_get_local_ip(uint32_t *out)
{
	if (out == NULL || !net_stack_up)
		return false;

	SceNetCtlInfo info;
	if (sce_failed(sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info)))
		return false;

	SceNetInAddr addr;
	if (sceNetInetPton(SCE_NET_AF_INET, info.ip_address, &addr) != 1 || addr.s_addr == 0)
		return false;

	*out = addr.s_addr;
	return true;
}

int SDLNet_GetLocalAddresses(IPaddress *addresses, int maxcount)
{
	if (addresses == NULL || maxcount < 1)
		return 0;

	uint32_t host = 0;
	if (!vita_get_local_ip(&host))
		return 0;

	addresses[0].host = host;
	addresses[0].port = 0;
	return 1;
}

UDPsocket SDLNet_UDP_Open(Uint16 port)
{
	if (!net_stack_up)
	{
		SDL_SetError("SDLNet_UDP_Open called before SDLNet_Init");
		return NULL;
	}

	UDPsocket sock = calloc(1, sizeof(*sock));
	if (sock == NULL)
	{
		SDL_SetError("out of memory");
		return NULL;
	}

	sock->fd = sceNetSocket("opentyrian", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
	if (sock->fd < 0)
	{
		SDL_SetError("sceNetSocket failed (0x%08X)", (unsigned int)sock->fd);
		free(sock);
		return NULL;
	}

	// Always bind, even for port 0: that is how we get an ephemeral port assigned, and it is
	// what SDL_net does.
	SceNetSockaddrIn sa;
	fill_sockaddr(&sa, SCE_NET_INADDR_ANY, SDL_SwapBE16(port));

	const int ret = sceNetBind(sock->fd, (const SceNetSockaddr *)&sa, sizeof(sa));
	if (sce_failed(ret))
	{
		SDL_SetError("sceNetBind on port %u failed (0x%08X)", (unsigned)port, (unsigned int)ret);
		sceNetSocketClose(sock->fd);
		free(sock);
		return NULL;
	}

	// Non-blocking so SDLNet_UDP_Recv can answer "nothing waiting", and broadcast-capable so
	// LAN discovery can probe. Both are best-effort in SDL_net too, so neither is fatal.
	const int one = 1;
	sceNetSetsockopt(sock->fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &one, sizeof(one));
	sceNetSetsockopt(sock->fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &one, sizeof(one));

	return sock;
}

void SDLNet_UDP_Close(UDPsocket sock)
{
	if (sock == NULL)
		return;

	sceNetSocketClose(sock->fd);
	free(sock);
}

int SDLNet_UDP_Bind(UDPsocket sock, int channel, const IPaddress *address)
{
	if (sock == NULL || address == NULL || channel < 0 || channel >= SDLNET_MAX_UDPCHANNELS)
	{
		SDL_SetError("invalid channel");
		return -1;
	}

	sock->channel_addr[channel] = *address;
	sock->channel_bound[channel] = true;
	return channel;
}

int SDLNet_UDP_Send(UDPsocket sock, int channel, UDPpacket *packet)
{
	if (sock == NULL || packet == NULL)
		return 0;

	IPaddress dest;
	if (channel < 0)
	{
		dest = packet->address;
	}
	else
	{
		if (channel >= SDLNET_MAX_UDPCHANNELS || !sock->channel_bound[channel])
		{
			SDL_SetError("channel %d is not bound", channel);
			return 0;
		}
		dest = sock->channel_addr[channel];
	}

	SceNetSockaddrIn sa;
	fill_sockaddr(&sa, dest.host, dest.port);

	const int ret = sceNetSendto(sock->fd, packet->data, (unsigned int)packet->len, 0,
	                             (const SceNetSockaddr *)&sa, sizeof(sa));

	packet->status = ret;
	if (sce_failed(ret))
	{
		SDL_SetError("sceNetSendto failed (0x%08X)", (unsigned int)ret);
		return 0;
	}

	return 1;
}

int SDLNet_UDP_Recv(UDPsocket sock, UDPpacket *packet)
{
	if (sock == NULL || packet == NULL)
		return -1;

	SceNetSockaddrIn sa;
	unsigned int salen = sizeof(sa);
	memset(&sa, 0, sizeof(sa));

	const int ret = sceNetRecvfrom(sock->fd, packet->data, (unsigned int)packet->maxlen, 0,
	                               (SceNetSockaddr *)&sa, &salen);

	packet->status = ret;

	if (sce_would_block(ret))
		return 0;

	if (sce_failed(ret))
	{
		SDL_SetError("sceNetRecvfrom failed (0x%08X)", (unsigned int)ret);
		return -1;
	}

	packet->len = ret;
	packet->address.host = sa.sin_addr.s_addr;
	packet->address.port = sa.sin_port;

	// Report which channel the sender is bound to, the way SDL_net does: network.c uses this
	// to tell an established peer's traffic from a stranger's.
	packet->channel = -1;
	for (int i = 0; i < SDLNET_MAX_UDPCHANNELS; ++i)
	{
		if (sock->channel_bound[i] &&
		    sock->channel_addr[i].host == packet->address.host &&
		    sock->channel_addr[i].port == packet->address.port)
		{
			packet->channel = i;
			break;
		}
	}

	return 1;
}

UDPpacket *SDLNet_AllocPacket(int size)
{
	if (size < 0)
		return NULL;

	UDPpacket *packet = calloc(1, sizeof(*packet));
	if (packet == NULL)
	{
		SDL_SetError("out of memory");
		return NULL;
	}

	packet->data = calloc(1, (size_t)size + 1);
	if (packet->data == NULL)
	{
		free(packet);
		SDL_SetError("out of memory");
		return NULL;
	}

	packet->maxlen = size;
	packet->channel = -1;
	return packet;
}

void SDLNet_FreePacket(UDPpacket *packet)
{
	if (packet == NULL)
		return;

	free(packet->data);
	free(packet);
}

#endif // __vita__ && WITH_NETWORK
