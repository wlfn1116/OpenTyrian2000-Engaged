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
#ifndef LVLLIB_H
#define LVLLIB_H

#include "opentyr.h"

typedef JE_longint JE_LvlPosType[43]; /* [1..42 + 1] */

extern JE_LvlPosType lvlPos;
extern char levelFile[13]; /* string [12] */
extern JE_word lvlNum;

void JE_analyzeLevel(void);
unsigned int JE_levelFileCount(int episode);
bool JE_levelFileNumValid(JE_word fileNum);

#endif /* LVLLIB_H */
