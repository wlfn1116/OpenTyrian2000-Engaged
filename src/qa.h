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
extern bool qa_fast_forward;

int qa_run_unit_suite(void);
int qa_run_replay_fixture(void);

// Shared check primitive, so a suite can live in its own translation unit.
void qa_check(bool okay, const char *what);
// Online Endless co-op: outpost, E-shop, perks, drives, downed/revive, restart (qa_endless.c).
void qa_test_endless_suite(void);
// Online Arcade and Campaign: the mode-flag split, two-wallet economy, records (qa_online.c).
void qa_test_online_suite(void);

#endif /* QA_H */
