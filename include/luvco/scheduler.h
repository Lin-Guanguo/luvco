#pragma once
#include <luvco/base.h>

#include <uv.h>

typedef struct luvco_scheduler luvco_scheduler;

size_t luvco_scheduler_sizeof (int nprocess);

void luvco_scheduler_init (luvco_scheduler* s, int nprocess);

// add work
int luvco_scheduler_addwork (luvco_scheduler* s, luvco_lstate* l);

// resume a work after luvco_yield_thread
int luvco_scheduler_resumework (luvco_scheduler* s, luvco_lstate* l);

int luvco_scheduler_totalwork (luvco_scheduler* s);


typedef struct luvco_uvwork luvco_uvwork;

typedef void (*luvco_uvwork_cb) (luvco_uvwork* work);

typedef struct luvco_uvwork {
    luvco_uvwork_cb cb;
} luvco_uvwork;

void luvco_add_uvwork(luvco_gstate* gstate, luvco_uvwork* uvwork);
