/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Deterministic sine and cosine for values that reach rollback state or a netplay hash. Platform
 * libm implementations may differ; these functions use only IEEE 754 basic operations. Presentation
 * code may continue using libm. sqrtf already has the required rounding guarantee.
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
#ifndef SIM_MATH_H
#define SIM_MATH_H

float sim_sinf(float x);
float sim_cosf(float x);

#endif /* SIM_MATH_H */
