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
#ifndef WEAPON_EDITOR_H
#define WEAPON_EDITOR_H

#include "opentyr.h"
#include "config_file.h"

bool JE_weaponEditor(bool fromOutpost);
int weaponEditorRangeShip(int ship);
void weaponEditorRelease(void);
bool weaponEditorOutpostMusicSelfTest(void);
bool weaponEditorSkinPreviewSelfTest(char *detail, size_t detailSize);
void weaponEditorConfigLoad(const ConfigSection *section);
void weaponEditorConfigSave(ConfigSection *section);

#endif // WEAPON_EDITOR_H
