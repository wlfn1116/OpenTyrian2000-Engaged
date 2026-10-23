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
#ifndef INGAME_MENU_H
#define INGAME_MENU_H

#include "opentyr.h"

void JE_doInGameSetup(void);
JE_boolean JE_inGameSetup(void);

// Endless death prompt (Hardcore off only), shown over the frozen death frame before the run
// summary. Values double as the row order.
typedef enum
{
	ENDLESS_DEATH_RESTART = 0,  // fly the same zone again from the launch-time snapshot
	ENDLESS_DEATH_OUTPOST,      // back to the outpost, exactly like the pause menu's Quit Level
	ENDLESS_DEATH_END_RUN,      // on to the Run Over summary
}
EndlessDeathChoice;

EndlessDeathChoice JE_endlessDeathMenu(void);
bool JE_extraMenu(void);
bool extraShipsAllowedInGame(void);
void extraShipReturnReset(void);
void extraShipRememberStandard(uint pnum);

#endif /* INGAME_MENU_H */
