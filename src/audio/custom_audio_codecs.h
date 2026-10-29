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
#ifndef CUSTOM_AUDIO_CODECS_H
#define CUSTOM_AUDIO_CODECS_H

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC system_header
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#define DR_MP3_NO_STDIO

#ifdef CUSTOM_AUDIO_CODECS_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#else
#define STB_VORBIS_HEADER_ONLY
#endif

#include "../../stuff/stb_vorbis-1.22/stb_vorbis.c"
#include "../../stuff/dr_mp3-0.7.3/dr_mp3.h"

#if !defined(__GNUC__) && !defined(__clang__) && defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif /* CUSTOM_AUDIO_CODECS_H */
