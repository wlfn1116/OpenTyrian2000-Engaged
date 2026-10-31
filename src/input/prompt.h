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
#ifndef PROMPT_H
#define PROMPT_H

#include "opentyr.h"

#include "joystick.h"

#include <stddef.h>

typedef enum
{
	PROMPT_KEYBOARD,
	PROMPT_TOUCH,
	PROMPT_PAD,
}
PromptDevice;

typedef struct
{
	const char *keys;
	const char *pad;
	const char *touch;
}
PromptText;

void prompt_use_keyboard(void);
void prompt_use_touch(void);
void prompt_use_pad(int j);

PromptDevice prompt_device(void);
PadLabels prompt_pad_labels(void);

const char *prompt_expand(char *buf, size_t size, const char *tmpl);
const char *prompt_text(char *buf, size_t size, const PromptText *text);
const char *prompt_binding_label(char *buf, size_t size, int j, const Joystick_assignment *binding);
const char *prompt_assignments_label(char *buf, size_t size, int j,
                                     const Joystick_assignment *assignments, int width);

void prompt_force(PromptDevice device, PadLabels labels);
void prompt_unforce(void);

typedef int (*PromptMeasure)(const char *line, const void *context);

const char *prompt_measure_fault(const PromptText *texts, size_t count, PromptMeasure measure,
                                 const void *context, int limit, const char *where);
const char *prompt_fit_fault(const PromptText *texts, size_t count, unsigned int font, int width,
                             const char *where);

#endif /* PROMPT_H */
