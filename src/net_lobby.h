/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef NET_LOBBY_H
#define NET_LOBBY_H

#include "opentyr.h"

/* In-game multiplayer setup: host on a chosen port, or join by address.  Replaces the
 * command-line-only path that was the original engine's sole way into a network game.
 *
 * Returns true when a connection is established and the caller should start a network game
 * (isNetworkGame, thisPlayerNum and the host's settings are all set up by then).  Returns
 * false when the player backed out or the attempt failed, in which case nothing is left
 * initialised and the title screen should simply be shown again.
 */
bool networkLobby(void);

#endif /* NET_LOBBY_H */
