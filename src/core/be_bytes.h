/* Byte-wise big-endian access for unaligned packed fields. */
#ifndef BE_BYTES_H
#define BE_BYTES_H

#include "SDL_types.h"

static inline void be_write16(Uint16 value, void *areap)
{
	Uint8 *area = (Uint8 *)areap;
	area[0] = (Uint8)(value >> 8);
	area[1] = (Uint8)value;
}

static inline void be_write32(Uint32 value, void *areap)
{
	Uint8 *area = (Uint8 *)areap;
	area[0] = (Uint8)(value >> 24);
	area[1] = (Uint8)(value >> 16);
	area[2] = (Uint8)(value >> 8);
	area[3] = (Uint8)value;
}

static inline Uint16 be_read16(const void *areap)
{
	const Uint8 *area = (const Uint8 *)areap;
	return (Uint16)(((Uint16)area[0] << 8) | area[1]);
}

static inline Uint32 be_read32(const void *areap)
{
	const Uint8 *area = (const Uint8 *)areap;
	return ((Uint32)area[0] << 24) | ((Uint32)area[1] << 16) | ((Uint32)area[2] << 8) | area[3];
}

static inline void be_write64(Uint64 value, void *areap)
{
	Uint8 *area = (Uint8 *)areap;
	for (int i = 0; i < 8; ++i)
		area[i] = (Uint8)(value >> (56 - 8 * i));
}

static inline Uint64 be_read64(const void *areap)
{
	const Uint8 *area = (const Uint8 *)areap;
	Uint64 value = 0;
	for (int i = 0; i < 8; ++i)
		value = (value << 8) | area[i];
	return value;
}

#endif /* BE_BYTES_H */
