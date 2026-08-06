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
// Which two-peer wire scenario to run: 0 base, 1 online campaign, 2 online Endless.
extern int qa_net_scenario;
extern bool qa_fast_forward;

int qa_run_unit_suite(void);
int qa_run_replay_fixture(void);

// Shared check primitive, so a suite can live in its own translation unit.
void qa_check(bool okay, const char *what);
// Online Endless co-op: outpost, E-shop, perks, drives, downed/revive, restart (qa_endless.c).
void qa_test_endless_suite(void);
// Online Arcade and Campaign: the mode-flag split, two-wallet economy, records (qa_online.c).
void qa_test_online_suite(void);

/* Two-peer wire scenarios (qa_net.c), run by network_test_peer under the hostile proxy in
 * testing/network_fault_test.py. Zero on success; both peers assert what they see of the other. */
int qa_net_campaign_phases(void);
int qa_net_endless_phases(void);

#endif /* QA_H */
