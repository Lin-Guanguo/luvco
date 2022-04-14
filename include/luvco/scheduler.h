#pragma once
#include <luvco/tools.h>

#include <uv.h>

typedef struct luvco_scheduler luvco_scheduler;

size_t luvco_scheduler_sizeof (int nprocess);

void luvco_scheduler_init (luvco_scheduler* s, int nprocess);

// add work
int luvco_scheduler_addwork (luvco_scheduler* s, luvco_lstate* l);

// resume a work after luvco_yield_thread
int luvco_scheduler_resumework (luvco_scheduler* s, luvco_lstate* l);

int luvco_scheduler_totalwork (luvco_scheduler* s);