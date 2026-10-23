/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Deterministic trigonometry for simulation code. Cody-Waite reduction and fdlibm kernels use
 * only IEEE 754 operations; see sim_math.h.
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
#include "sim_math.h"

/* Split pi/2 to preserve low bits for unbounded sidekick angles. */
#define PIO2_HI  1.57079632673412561417e+00
#define PIO2_LO  6.07710050650619224932e-11
#define TWO_OVER_PI 6.36619772367581382433e-01

/* This unreachable clamp keeps range reduction within long long. */
#define SIM_TRIG_MAX 1.0e12

/* sin(r) on [-pi/4, pi/4] */
static double sim_kernel_sin(double r)
{
	const double r2 = r * r;
	const double p = -1.66666666666666324348e-01
	    + r2 * (8.33333333332248946124e-03
	    + r2 * (-1.98412698298579493134e-04
	    + r2 * (2.75573137070700676789e-06
	    + r2 * (-2.50507602534068634195e-08
	    + r2 * 1.58969099521155010221e-10))));
	return r + r * r2 * p;
}

/* cos(r) on [-pi/4, pi/4] */
static double sim_kernel_cos(double r)
{
	const double r2 = r * r;
	const double p = 4.16666666666666019037e-02
	    + r2 * (-1.38888888888741095749e-03
	    + r2 * (2.48015872894767294178e-05
	    + r2 * (-2.75573143513906633035e-07
	    + r2 * (2.08757232129817482790e-09
	    + r2 * -1.13596475577881948265e-11))));
	return 1.0 - 0.5 * r2 + r2 * r2 * p;
}

/* Reduce x to r in [-pi/4, pi/4]; returns the quadrant 0..3. */
static int sim_reduce(double x, double *r)
{
	if (x > SIM_TRIG_MAX)
		x = SIM_TRIG_MAX;
	else if (x < -SIM_TRIG_MAX)
		x = -SIM_TRIG_MAX;

	const double t = x * TWO_OVER_PI;
	const double n = (double)(long long)(t >= 0.0 ? t + 0.5 : t - 0.5);

	/* Two steps, high part first: n * PIO2_HI cancels against x exactly, so the
	 * subtraction keeps full precision and only the tiny PIO2_LO term remains. */
	*r = (x - n * PIO2_HI) - n * PIO2_LO;

	/* Via unsigned so the negative case is a defined two's-complement wrap, not a
	 * bitwise-AND on a negative signed value. */
	return (int)((unsigned long long)(long long)n & 3u);
}

float sim_sinf(float x)
{
	double r;
	switch (sim_reduce((double)x, &r))
	{
	case 0:  return (float)sim_kernel_sin(r);
	case 1:  return (float)sim_kernel_cos(r);
	case 2:  return (float)-sim_kernel_sin(r);
	default: return (float)-sim_kernel_cos(r);
	}
}

float sim_cosf(float x)
{
	double r;
	switch (sim_reduce((double)x, &r))
	{
	case 0:  return (float)sim_kernel_cos(r);
	case 1:  return (float)-sim_kernel_sin(r);
	case 2:  return (float)-sim_kernel_cos(r);
	default: return (float)sim_kernel_sin(r);
	}
}
