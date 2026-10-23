/* 32-bit FNV-1a hashing. */
#ifndef FNV1A_H
#define FNV1A_H

#include "SDL_types.h"

#include <stddef.h>

#define FNV1A32_INIT 2166136261u

static inline Uint32 fnv1a32_update(Uint32 h, const void *data, size_t len)
{
	const Uint8 *const p = (const Uint8 *)data;
	for (size_t i = 0; i < len; ++i)
	{
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}

static inline Uint32 fnv1a32(const void *data, size_t len)
{
	return fnv1a32_update(FNV1A32_INIT, data, len);
}

// Hashes the word's four bytes, least significant first.
static inline Uint32 fnv1a32_u32(Uint32 h, Uint32 w)
{
	for (int b = 0; b < 4; ++b)
	{
		h ^= (w >> (b * 8)) & 0xffu;
		h *= 16777619u;
	}
	return h;
}

#endif /* FNV1A_H */
