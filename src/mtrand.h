/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
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
#ifndef MTRAND_H
#define MTRAND_H

#include <stdint.h>

#define MT_RAND_MAX 0xffffffffUL

/* The generator is 32-bit throughout; the `unsigned long` in these two signatures is
 * historical and harmless (every value fits, and every caller takes a modulus).  The
 * STATE is what has to be fixed-width; see the snapshot note below. */
void mt_srand(unsigned long s);
unsigned long mt_rand(void);
float mt_rand_1(void);
float mt_rand_lt1(void);

/* Draw count since mt_srand(), included in network desync diagnostics. */
extern uint32_t mt_rand_count;

/* Rollback stores the full generator state as a fixed-width blob. Platform-sized
 * integer types would make the network recovery layout incompatible. */
#include <stddef.h>
size_t mt_state_size(void);
void mt_state_save(void *dst);
void mt_state_restore(const void *src);

#endif /* MTRAND_H */
