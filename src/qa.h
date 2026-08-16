/* Automated correctness harness entry points. */
#ifndef QA_H
#define QA_H

#include "opentyr.h"

#include <stdbool.h>

extern bool qa_test_suite;
extern const char *qa_fixture_dir;
extern int qa_replay_demo;
extern unsigned long qa_replay_ticks;
/* Ticks of headless Destruct to run with every frame replayed from its own snapshot; zero is off.
 * Proves the rollback snapshot covers the whole battle. */
extern unsigned long qa_destruct_selftest_ticks;
extern bool qa_replay_expect_set;
extern Uint32 qa_replay_expect;
/* Fly the replayed demo with Endless effects armed, so the self-test's frame-by-frame comparison
 * reaches a half of the game it otherwise skips: 1 also grants Chain Reaction at full stacks and
 * covers the pulse queue and a wave crossing a snapshot, 2 is the same level without the perk and
 * is the control for what 1 reports. The state hash is not the campaign one and is not a fixture;
 * the failure count is the result. */
extern int qa_replay_chain;
extern int qa_net_rounds;
// Which two-peer wire scenario to run: 0 base, 1 online campaign, 2 online Endless,
// 3 barrier storm.
extern int qa_net_scenario;
// Offset added to the wire version this test peer reports, to drive the mismatch rejection.
extern int qa_net_version_skew;
/* Two-peer gameplay: nonzero runs a real level for this many frames with scripted movement,
 * then reports and exits. corrupt_frame bends one frame of epoch 0 on this machine, so the
 * canary has a real divergence to find and the recovery stream something to repair. */
extern unsigned long qa_net_gameplay_ticks;
// Presented Delay-Based frames, used by scenario 19's bounded non-rollback verdict.
extern unsigned long qa_net_delay_frames;
extern unsigned long qa_net_special_flashes;
extern unsigned long qa_net_corrupt_frame;
/* Save/resume across the wire: save_exit writes the LAST LEVEL slot as a passing gameplay run
 * exits; resume_slot makes the host auto-load that slot, so the joiner adopts the resume form. */
extern bool qa_net_save_exit;
extern int qa_net_resume_slot;
// Sidekick mount profile for the gameplay wire tests; 0 keeps the stock loadout.
extern int qa_net_loadout;
void qa_net_apply_loadout(int profile);
void qa_net_apply_linked_special(void);

/* Simultaneous in-game-menu race: both peers raise the request on this frame (0 = off), which
 * is the documented both-players-press-Esc case host-wins arbitration has to settle. */
extern unsigned long qa_net_menu_frame;
// Game type for a command-line gameplay run: -1 keeps Arcade, else a NetworkGameType value.
extern int qa_net_game_type;
/* Multi-level gameplay runs: fly until this many levels/zones have been CLEARED (each level is
 * ended by script at QA_NET_ZONE_END_FRAME), then report the session verdict at the next
 * outpost. 0 keeps the plain bounded flight that reports at the tick limit. */
extern int qa_net_zones;
extern int qa_net_zones_cleared;
#define QA_NET_ZONE_END_FRAME 500
/* Lobby-settings run: peers take the production lobby roles, the host arms Individual credit
 * plus Double Earnings from its own config, and the joiner adopts the settings block from the
 * connect packet; scripted in-sim pickups then drive the doubled payment rule on both sims. */
extern bool qa_net_lobby_settings;
/* Separate-arcade run: both peers arm the host's Separate ships setting, so the flight exercises
 * two independent arcade ships (own lives, guns, sidekicks, specials and generator) across the
 * wire. Any per-ship state left in a shared global desyncs the pair. */
extern bool qa_net_arcade_separate;
/* SuperTyrian wire runs: fly the Scrollock variant rather than the standard one. Both peers must
 * be given it, since a test peer has no lobby to publish it from. */
extern bool qa_net_scrollock;
/* True while a lobby-settings wire run keeps command-line peers under the lobby roles: the
 * main loop must still treat them as command-line (no title screen, no lobby teardown). */
static inline bool qa_net_lobby_run(void)
{
	return qa_net_gameplay_ticks > 0 && qa_net_lobby_settings;
}
// Forced modifier slate for the Endless zone at this depth (identical on both machines).
Uint64 qa_net_zone_mods(int depth);
// Zones target reached: print the session verdict (net_rollback.c) and exit the peer.
void qa_net_zone_verdict(void);
extern bool qa_fast_forward;

int qa_run_unit_suite(void);
int qa_run_replay_fixture(void);
int qa_run_destruct_selftest(void);

// Shared check primitive, so a suite can live in its own translation unit.
void qa_check(bool okay, const char *what);
// Online Endless co-op: outpost, E-shop, perks, drives, downed/revive, restart (qa_endless.c).
void qa_test_endless_suite(void);
// Online Arcade and Campaign: the mode-flag split, two-wallet economy, records (qa_online.c).
void qa_test_online_suite(void);
// Lobby row, value, help and action strings against their width budgets (net_lobby.c).
void qa_test_net_lobby_strings(void);
// The Relaxed death prompt's rows and widths against the choice enum (mainint.c).
void qa_test_endless_death_menu(void);
// The special-meter fired/ready edges across the linked pair's two movement passes (mainint.c).
void qa_test_special_light_events(void);
// The Chain Reaction pulse queue: reach, the cascade, and what stops it (tyrian2.c).
void qa_test_chain_cascade(void);
/* Drive a chain wave through the real pulse queue and drain (tyrian2.c, which owns both): a row of
 * `count` enemies worth `evalue` each, spaced so the wave carries, started by a pulse belonging to
 * `owner`. Reports the cash each ship took over the whole wave, how many of the row it killed, and
 * whether drops appeared. Clears the enemy table; callers restore it. */
void qa_chain_kill_row(int owner, int evalue, int count,
                       long *out_paid0, long *out_paid1, int *out_killed, bool *out_dropped);

/* Two-peer wire scenarios (qa_net.c), run by network_test_peer under the hostile proxy in
 * testing/network_fault_test.py. Zero on success; both peers assert what they see of the other. */
int qa_net_campaign_phases(void);
int qa_net_endless_phases(void);
int qa_net_barrier_phases(void);

#endif /* QA_H */
