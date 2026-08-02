/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
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
#ifndef MTRAND_H
#define MTRAND_H

#include <stdint.h>

#define MT_RAND_MAX 0xffffffffUL

/* The generator is 32-bit throughout; the `unsigned long` in these two signatures is
 * historical and harmless (every value fits, and every caller takes a modulus).  The
 * STATE is what has to be fixed-width -- see the snapshot note below. */
void mt_srand(unsigned long s);
unsigned long mt_rand(void);
float mt_rand_1(void);
float mt_rand_lt1(void);

/* Draws taken since the last mt_srand(). Netplay's desync detector hashes this: any
 * divergence in how much randomness the two sims consumed shows up here immediately,
 * usually a tick or two before it becomes visible in positions or armor. Network levels
 * reseed to a fixed constant (tyrian2.c), so the count is directly comparable. */
extern uint32_t mt_rand_count;

/* Rollback snapshot support: the generator's full state (vector, cursors as
 * offsets, draw count) as an opaque same-process blob.  See mtrand.c.
 *
 * Fixed-width, not `unsigned long`: netplay desync recovery ships this blob to the
 * peer, and `unsigned long` made the state vector 2496 bytes on Windows and 4992 on
 * the consoles.  That alone put a PC<->Switch session 2500 bytes apart, so the
 * receiving side refused every recovery stream it was ever offered. */
#include <stddef.h>
size_t mt_state_size(void);
void mt_state_save(void *dst);
void mt_state_restore(const void *src);

#endif /* MTRAND_H */
