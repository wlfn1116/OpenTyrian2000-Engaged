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
#ifndef PCXMAST_H
#define PCXMAST_H

#include "opentyr.h"

#define PCX_NUM 14

typedef JE_longint JE_pcxpostype[PCX_NUM + 1]; /* [1..PCXnum + 1] */

extern const JE_byte pcxpal[PCX_NUM];    /* [1..PCXnum] */
extern const JE_byte facepal[12];       /* [1..12] */
extern JE_pcxpostype pcxpos;

#endif /* PCXMAST_H */
