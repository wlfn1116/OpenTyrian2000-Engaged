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

#endif /* QA_H */
