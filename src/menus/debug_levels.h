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
#ifndef DEBUG_LEVELS_H
#define DEBUG_LEVELS_H

#include "opentyr.h"

#include <stddef.h>

bool JE_debugLevelSelect(void);

// Debug panel for live Endless effects and scaling.
void endlessDebugTuneScreen(void);

// The debug level browser drops straight into a level, skipping the campaign route that
// normally leads there.
bool debugLevelJumpTake(void);
void debugLevelJumpReturn(void);

// Stage, query, and adopt a debug-browser choice for the next online launch.
bool debugLevelPickGet(JE_byte *episode, JE_byte *section, JE_byte *fileNum);
void debugLevelPickApply(JE_byte episode, JE_byte section, JE_byte fileNum);
void debugLevelPickReset(void);

/* Stage and adopt the whole Endless debug block paired with a network level jump. */
bool endlessJumpPickGet(Uint8 *block);              // fills ENDLESS_DEBUG_BLOCK_SIZE bytes
void endlessJumpPickApply(const Uint8 *block, size_t len);
void endlessJumpPickStage(void);                    // capture the state this machine just applied
void endlessJumpPickReset(void);
const char *debugLevelsPromptFault(void);

#endif // DEBUG_LEVELS_H
