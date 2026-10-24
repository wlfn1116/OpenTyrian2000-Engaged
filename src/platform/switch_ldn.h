/* Nintendo Switch local wireless (LDN) networks, for console-to-console play without a router.
 * Inert unless __SWITCH__ is defined. */
#ifndef SWITCH_LDN_H
#define SWITCH_LDN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __SWITCH__

#define SWITCH_LDN_MAX_GAMES 8

// LDN takes its network id from the title that launched the homebrew, so every console has to
// start the game the same way: through the same game, or all from the Album.

// Create a network and advertise `advert` on it. Call before network_init() so the game socket
// opens on the new network. switch_ldn_leave() closes it.
bool switch_ldn_host(const char *nickname, const uint8_t *advert, size_t advert_len);

// Scan for networks this game created; returns how many, at most SWITCH_LDN_MAX_GAMES. The
// console stays in station mode until switch_ldn_join() or switch_ldn_leave().
int switch_ldn_scan(void);

// Advertisement of scan result `index`.
bool switch_ldn_game(int index, const uint8_t **advert, size_t *advert_len);

// Join scan result `index`. On success *host is the hosting console's IPv4 address with the
// first octet in the top byte.
bool switch_ldn_join(int index, const char *nickname, uint32_t *host);

// Leave or close whatever network is up and release the service. Safe to call at any time.
void switch_ldn_leave(void);

#endif // __SWITCH__

#endif // SWITCH_LDN_H
