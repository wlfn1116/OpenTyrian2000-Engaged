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
#ifndef SWAR_H
#define SWAR_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if UINTPTR_MAX >= UINT64_MAX
typedef uint64_t Swar8;
#define SWAR8_LANES 8
#else
typedef uint32_t Swar8;
#define SWAR8_LANES 4
#endif

#define SWAR8_BYTES ((int)sizeof(Swar8))
#define SWAR8(x) ((Swar8)((Swar8)(uint8_t)(x) * (Swar8)((Swar8)~(Swar8)0 / 0xFFU)))

static inline Swar8 swar8_read(const uint8_t *src)
{
	Swar8 v;
	memcpy(&v, src, sizeof(v));
	return v;
}

static inline void swar8_write(uint8_t *dst, Swar8 v)
{
	memcpy(dst, &v, sizeof(v));
}

static inline Swar8 swar8_nonzero(Swar8 v)
{
	const Swar8 bit = (v | ((v & SWAR8(0x7F)) + SWAR8(0x7F))) & SWAR8(0x80);
	return bit | (bit - (bit >> 7));
}

static inline Swar8 swar8_select(Swar8 a, Swar8 b, Swar8 mask)
{
	return a ^ ((a ^ b) & mask);
}

static inline Swar8 swar8_avg_low(Swar8 a, Swar8 b)
{
	return (((a & SWAR8(0x0F)) + (b & SWAR8(0x0F))) >> 1) & SWAR8(0x0F);
}

static inline Swar8 swar8_reverse(Swar8 v)
{
#if SWAR8_LANES == 8
#if defined(_MSC_VER) && !defined(__clang__)
	return _byteswap_uint64(v);
#elif defined(__GNUC__) || defined(__clang__)
	return __builtin_bswap64(v);
#else
	v = ((v & 0x00FF00FF00FF00FFull) << 8) | ((v >> 8) & 0x00FF00FF00FF00FFull);
	v = ((v & 0x0000FFFF0000FFFFull) << 16) | ((v >> 16) & 0x0000FFFF0000FFFFull);
	return (v << 32) | (v >> 32);
#endif
#else
#if defined(_MSC_VER) && !defined(__clang__)
	return _byteswap_ulong(v);
#elif defined(__GNUC__) || defined(__clang__)
	return __builtin_bswap32(v);
#else
	v = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
	return (v << 16) | (v >> 16);
#endif
#endif
}

#endif /* SWAR_H */
