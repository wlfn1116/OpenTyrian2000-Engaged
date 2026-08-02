/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Deterministic trigonometry for SIMULATION code.
 *
 * IEEE 754 pins +, -, *, / and comparisons to an exact result, so any two builds agree
 * on them bit for bit.  It says nothing about sinf/cosf: those are library code, and
 * MSVC's CRT and the consoles' newlib are free to return different values for the same
 * input.  Anything a shot, an enemy or a sidekick derives from one therefore risks a
 * cross-platform desync that no RNG check can see -- the same shape of bug as the
 * unsequenced mt_rand() pairs (notes.md, § Determinism harness).
 *
 * These two are built only from the exact operations, so every platform gets the same
 * bits.  They are accurate to well under half a float ULP, so on any platform whose libm
 * is correctly rounded they return exactly what sinf/cosf already did -- the trajectories
 * players see do not move.
 *
 * USE THESE whenever the result reaches registered rollback state or a netplay hash:
 * shot velocities and spawn positions, enemy shot fans, sidekick positions.  Presentation
 * code (shop previews, render interpolation, spark showers -- superpixels are outside the
 * registry by design) can keep the libm calls; it costs nothing either way.
 *
 * sqrtf needs no replacement: IEEE 754 specifies it as correctly rounded, like the
 * basic operations.
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
#ifndef SIM_MATH_H
#define SIM_MATH_H

float sim_sinf(float x);
float sim_cosf(float x);

#endif /* SIM_MATH_H */
