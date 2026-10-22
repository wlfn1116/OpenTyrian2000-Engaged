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

#include <stdbool.h>
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

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) \
    || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>

typedef __m128i Vec8;
#define VEC8_BYTES 16

static inline Vec8 vec8_read(const uint8_t *src)
{
	return _mm_loadu_si128((const __m128i *)src);
}

static inline void vec8_write(uint8_t *dst, Vec8 v)
{
	_mm_storeu_si128((__m128i *)dst, v);
}

static inline Vec8 vec8_splat(uint8_t x)
{
	return _mm_set1_epi8((char)x);
}

static inline Vec8 vec8_and(Vec8 a, Vec8 b)
{
	return _mm_and_si128(a, b);
}

static inline Vec8 vec8_andnot(Vec8 a, Vec8 b)
{
	return _mm_andnot_si128(b, a);
}

static inline Vec8 vec8_or(Vec8 a, Vec8 b)
{
	return _mm_or_si128(a, b);
}

static inline Vec8 vec8_add(Vec8 a, Vec8 b)
{
	return _mm_add_epi8(a, b);
}

static inline Vec8 vec8_sub(Vec8 a, Vec8 b)
{
	return _mm_sub_epi8(a, b);
}

static inline Vec8 vec8_shl1(Vec8 v)
{
	return _mm_add_epi8(v, v);
}

static inline Vec8 vec8_shr1(Vec8 v)
{
	return _mm_and_si128(_mm_srli_epi16(v, 1), _mm_set1_epi8(0x7F));
}

static inline Vec8 vec8_shr2(Vec8 v)
{
	return _mm_and_si128(_mm_srli_epi16(v, 2), _mm_set1_epi8(0x3F));
}

static inline Vec8 vec8_shr4(Vec8 v)
{
	return _mm_and_si128(_mm_srli_epi16(v, 4), _mm_set1_epi8(0x0F));
}

static inline Vec8 vec8_nonzero(Vec8 v)
{
	return _mm_xor_si128(_mm_cmpeq_epi8(v, _mm_setzero_si128()), _mm_set1_epi8(-1));
}

static inline Vec8 vec8_select(Vec8 a, Vec8 b, Vec8 mask)
{
	return _mm_or_si128(_mm_andnot_si128(mask, a), _mm_and_si128(mask, b));
}

static inline bool vec8_is_zero(Vec8 v)
{
	return _mm_movemask_epi8(_mm_cmpeq_epi8(v, _mm_setzero_si128())) == 0xFFFF;
}

static inline Vec8 vec8_reverse(Vec8 v)
{
	v = _mm_shuffle_epi32(v, _MM_SHUFFLE(0, 1, 2, 3));
	v = _mm_shufflelo_epi16(v, _MM_SHUFFLE(2, 3, 0, 1));
	v = _mm_shufflehi_epi16(v, _MM_SHUFFLE(2, 3, 0, 1));
	return _mm_or_si128(_mm_slli_epi16(v, 8), _mm_srli_epi16(v, 8));
}

#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>

typedef uint8x16_t Vec8;
#define VEC8_BYTES 16

static inline Vec8 vec8_read(const uint8_t *src)
{
	return vld1q_u8(src);
}

static inline void vec8_write(uint8_t *dst, Vec8 v)
{
	vst1q_u8(dst, v);
}

static inline Vec8 vec8_splat(uint8_t x)
{
	return vdupq_n_u8(x);
}

static inline Vec8 vec8_and(Vec8 a, Vec8 b)
{
	return vandq_u8(a, b);
}

static inline Vec8 vec8_andnot(Vec8 a, Vec8 b)
{
	return vbicq_u8(a, b);
}

static inline Vec8 vec8_or(Vec8 a, Vec8 b)
{
	return vorrq_u8(a, b);
}

static inline Vec8 vec8_add(Vec8 a, Vec8 b)
{
	return vaddq_u8(a, b);
}

static inline Vec8 vec8_sub(Vec8 a, Vec8 b)
{
	return vsubq_u8(a, b);
}

static inline Vec8 vec8_shl1(Vec8 v)
{
	return vaddq_u8(v, v);
}

static inline Vec8 vec8_shr1(Vec8 v)
{
	return vshrq_n_u8(v, 1);
}

static inline Vec8 vec8_shr2(Vec8 v)
{
	return vshrq_n_u8(v, 2);
}

static inline Vec8 vec8_shr4(Vec8 v)
{
	return vshrq_n_u8(v, 4);
}

static inline Vec8 vec8_nonzero(Vec8 v)
{
	return vtstq_u8(v, v);
}

static inline Vec8 vec8_select(Vec8 a, Vec8 b, Vec8 mask)
{
	return vbslq_u8(mask, b, a);
}

static inline bool vec8_is_zero(Vec8 v)
{
	const uint64x2_t words = vreinterpretq_u64_u8(v);
	return (vgetq_lane_u64(words, 0) | vgetq_lane_u64(words, 1)) == 0;
}

static inline Vec8 vec8_reverse(Vec8 v)
{
	const uint8x16_t halves = vrev64q_u8(v);
	return vextq_u8(halves, halves, 8);
}

#else
#define VEC8_BYTES 0
#endif

#if VEC8_BYTES
static inline Vec8 vec8_avg_low(Vec8 a, Vec8 b)
{
	const Vec8 low = vec8_splat(0x0F);
	return vec8_shr1(vec8_add(vec8_and(a, low), vec8_and(b, low)));
}
#endif

#endif /* SWAR_H */
