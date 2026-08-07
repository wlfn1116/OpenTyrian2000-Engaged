/* Automated correctness harness entry points. */
#ifndef QA_H
#define QA_H

#include "opentyr.h"

#include <stdbool.h>

extern bool qa_test_suite;
extern const char *qa_fixture_dir;
extern int qa_replay_demo;
extern unsigned long qa_replay_ticks;
extern bool qa_replay_expect_set;
extern Uint32 qa_replay_expect;
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
extern unsigned long qa_net_corrupt_frame;
/* Save/resume across the wire: save_exit writes the LAST LEVEL slot as a passing gameplay run
 * exits; resume_slot makes the host auto-load that slot, so the joiner adopts the resume form. */
extern bool qa_net_save_exit;
extern int qa_net_resume_slot;
extern bool qa_fast_forward;

int qa_run_unit_suite(void);
int qa_run_replay_fixture(void);

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

/* Two-peer wire scenarios (qa_net.c), run by network_test_peer under the hostile proxy in
 * testing/network_fault_test.py. Zero on success; both peers assert what they see of the other. */
int qa_net_campaign_phases(void);
int qa_net_endless_phases(void);
int qa_net_barrier_phases(void);

#endif /* QA_H */
