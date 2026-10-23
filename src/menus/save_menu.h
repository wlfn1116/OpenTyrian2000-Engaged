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
#ifndef SAVE_MENU_H
#define SAVE_MENU_H

#include "opentyr.h"
#include "savegame.h"

// net2p pins it to the 2-player page for the online host and returns the loaded slot;
// saving turns it into a save menu (returns 0; the saving happens inside).
int JE_loadScreen(bool net2p, bool saving);
void JE_saveTransferUpload(void);
void JE_saveTransferDownload(void);
/* Which sessions may load a given record. The save pages show an incompatible slot dimmed and
 * unselectable rather than hiding it. */
bool save_type_compatible(const JE_SaveFileType *rec, JE_byte slot, bool net2p);
bool save_custom_locked(const JE_SaveFileType *rec);
void JE_operation(JE_byte slot);

extern JE_boolean performSave;

#endif /* SAVE_MENU_H */
