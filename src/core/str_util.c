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
#include "str_util.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

bool str_pop_int(char *str, int *val)
{
	bool success = false;

	char buf[256];
	assert(strlen(str) < sizeof(buf));

	// grab the value from str
	char *end;
	*val = strtol(str, &end, 10);

	if (end != str)
	{
		success = true;

		// shift the rest to the beginning
		strcpy(buf, end);
		strcpy(str, buf);
	}

	return success;
}
