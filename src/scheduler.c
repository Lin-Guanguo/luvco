#include <luvco.h>
#include <luvco/scheduler.h>

#include <uv.h>

#include <assert.h>
#include <stdlib.h>

typedef struct luvco_process_data {
    uv_thread_t thread;

    luvco_scheduler* scheduler;
} luvco_process_data;

typedef struct luvco_scheduler {
    int nprocess;

    luvco_ringbuf2* worklist; // element is luvco_lstate

    luvco_process_data pdata[];
} luvco_scheduler;


static void scheduler_thread_cb (void* arg) {
    luvco_process_data* pdata = (luvco_process_data*)arg;
    luvco_scheduler* scheduler = pdata->scheduler;
    luvco_ringbuf2* worklist = scheduler->worklist;
    for (;;) {
        luvco_lstate* lstate;
        lua_State* L;
        int ret = luvco_ringbuf2_pop(worklist, (void**)&lstate);
        if (ret != 0) {
            continue;
        }

        int resumeret = 0;
        while (luvco_ringbuf2_pop(lstate->toresume, (void**)&L) == 0) {
            log_trace("Thread %p resume L:%p", arg, L);
            resumeret = luvco_resume(L);
            if (resumeret != LUVCO_RESUME_NORMAL) break;
        }

        switch (resumeret) {
        case LUVCO_RESUME_NORMAL:
            luvco_ringbuf2_spinpush(worklist, lstate);
            break;
        case LUVCO_RESUME_ERROR:
            log_error("some error happen when resume L:%p", L);
            break;
        case LUVCO_RESUME_LSTATE_END:
            log_trace("all coro end lstate:%p, remove from process worklist", lstate);
            break;
        case LUVCO_RESUME_YIELD_THREAD:
            log_trace("yield thread L:%p, lstate:%p", L, lstate);
            break;
        default:
            assert(0 && "Unexpected resume return value");
        }
    }
}

#define WORKLIST_LEN 4
#define WORKLIST_LEN2  8

void luvco_scheduler_init (luvco_scheduler* s, int nprocess) {
    s->nprocess = nprocess;
    s->worklist = (luvco_ringbuf2*)malloc(luvco_ringbuf2_sizeof(WORKLIST_LEN));
    luvco_ringbuf2_init(s->worklist, WORKLIST_LEN, WORKLIST_LEN2);
    for (int i = 0; i < nprocess; i++) {
        luvco_process_data* pdata = &s->pdata[i];
        pdata->scheduler = s;
    }

    for (int i = 0; i < nprocess; i++) {
        int ret = uv_thread_create(&s->pdata[i].thread, scheduler_thread_cb, (void*)&s->pdata[i]);
        assert(ret == 0);
    }
}

size_t luvco_scheduler_sizeof (int nprocess) {
    return sizeof(luvco_scheduler) + sizeof(luvco_process_data) * nprocess;
}

int luvco_scheduler_addwork (luvco_scheduler* s, luvco_lstate* l) {
    luvco_ringbuf2_spinpush(s->worklist, (void*)l);
    return 0;
}
