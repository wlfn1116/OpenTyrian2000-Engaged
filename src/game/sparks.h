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
#ifndef SPARKS_H
#define SPARKS_H

#include "opentyr.h"

#define MAX_SUPERPIXELS          50000  // was 101; global spark ring buffer; bigger = denser/longer explosion showers
// Original DOS cap; Extra Sparks selects between this and MAX_SUPERPIXELS.
#define SUPERPIXELS_CLASSIC      101
#define SUPERPIXEL_SPAWN_Z       15   // shade and lifetime a spark starts with; JE_drawSP counts it down to 0

// `bright` raises superpixel shade; 0 keeps shipped shading.
typedef struct {
	unsigned int x, y, z;
	signed int delta_x, delta_y;
	Uint8 color, bright;
	bool occluded;
} superpixel_type;

extern superpixel_type superpixels[MAX_SUPERPIXELS];

// Optional clip window for JE_drawSP. While active, superpixels are only plotted (and recorded
// for smooth replay) inside [x0,x1) x [y0,y1).
extern bool superpixelClipActive;
extern int superpixelClipX0, superpixelClipY0, superpixelClipX1, superpixelClipY1;
void JE_setSPClip(int x0, int y0, int x1, int y1);
void JE_clearSPClip(void);

// Inclusive screen-space box that occludes marked sparks for one frame.
void JE_addSPOccluder(int x0, int y0, int x1, int y1);

// classic_cap keeps the shower inside the shipped 101-spark window.
void JE_doSP(JE_word x, JE_word y, JE_word num, JE_byte explowidth, JE_byte color, bool classic_cap);
// Uncapped JE_doSP with a shorter life. It keeps JE_doSP's simulation RNG cost; `bright`
// compensates for the lower initial z. Spawn it before JE_drawSP.
void JE_doSPBrief(JE_word x, JE_word y, JE_word num, JE_byte explowidth, JE_byte color,
                  JE_byte life, JE_byte bright);
// Seeded, presentation-only shower with optional shade lift and occlusion.
void JE_doSPSeeded(JE_word x, JE_word y, JE_word num, JE_byte explowidth, JE_byte color,
                   bool classic_cap, JE_byte bright, bool occluded, Uint32 seed);
// Presentation-only spark ring that reaches `radius` on its last life tick. `spacing` controls
// density, so larger rings receive more sparks.
void JE_doSPRingSeeded(JE_word x, JE_word y, JE_word radius, JE_byte spacing, JE_byte color,
                       JE_byte life, JE_byte bright, Uint32 seed);
// A bolt of motionless sparks strung from (x0,y0) to (x1,y1), `spacing` px apart and bowed off the
// straight line by up to `wander` px at its middle. Seeded and uncapped like JE_doSPRingSeeded.
void JE_doSPBoltSeeded(JE_word x0, JE_word y0, JE_word x1, JE_word y1, JE_byte spacing, JE_byte wander,
                       JE_byte color, JE_byte life, JE_byte bright, Uint32 seed);
// Sparks that run downward from (x,y), each starting within `spread` px of it and falling at its own
// pace, so the shower reads as a drip rather than a burst. Seeded and uncapped like the shapes above.
void JE_doSPDripSeeded(JE_word x, JE_word y, JE_word num, JE_byte spread, JE_byte color,
                       JE_byte life, JE_byte bright, Uint32 seed);
// Presentation-only spark pop; call while the enemy-shot slot is still live.
void enemy_shot_vaporise_sparks(unsigned int slot);
void JE_drawSP(void);
void JE_resetSP(void);
// Discard restores the spark ring to the state captured by begin.
void JE_beginSPPass(void);
void JE_discardSPPass(void);

#endif /* SPARKS_H */
