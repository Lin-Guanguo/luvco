#include <luvco/scheduler.h>
#include <luvco/ringbuf.h>

#include <uv.h>

#include <assert.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>

typedef struct luvco_process_data {
    uv_thread_t thread;

    luvco_scheduler* scheduler;
} luvco_process_data;

typedef struct luvco_scheduler {
    int nprocess;

    luvco_ringbuf2* worklist; // element is luvco_lstate

    atomic_int running_work;
    atomic_int yield_work;
    atomic_int total_work;

    volatile bool stop_flag;

    luvco_process_data pdata[];
} luvco_scheduler;


static void scheduler_thread_cb (void* arg) {
    luvco_process_data* pdata = (luvco_process_data*)arg;
    luvco_scheduler* s = pdata->scheduler;
    luvco_ringbuf2* worklist = s->worklist;
    int thread_i = (int)(pdata - s->pdata);
    for (;;) {
        luvco_lstate* lstate;
        lua_State* L;
        int ret = luvco_ringbuf2_pop(worklist, (void**)&lstate);
        if (ret != 0) {
            if (s->stop_flag) {
                log_debug("thread %d stop", thread_i);
                break;
            }
            continue;
        }

        int resumeret = 0;
        while (luvco_ringbuf2_pop(lstate->toresume, (void**)&L) == 0) {
            log_trace("thread %d resume L:%p", thread_i, L);
            resumeret = luvco_resume(L);
            if (resumeret != LUVCO_RESUME_NORMAL) break;
        }

        switch (resumeret) {
        case LUVCO_RESUME_NORMAL:
            luvco_ringbuf2_spinpush(worklist, lstate);
            break;
        case LUVCO_RESUME_ERROR:
            atomic_fetch_add(&s->running_work, -1);
            atomic_fetch_add(&s->total_work, -1);
            log_error("some error happen when resume L:%p", L);
            break;
        case LUVCO_RESUME_LSTATE_END:
            atomic_fetch_add(&s->running_work, -1);
            ret = atomic_fetch_add(&s->total_work, -1);
            log_trace("scheduler end lstate:%p, totalwork=%d, yieldwork=%d", lstate, ret-1, atomic_load(&s->yield_work));
            break;
        case LUVCO_RESUME_YIELD_THREAD:
            atomic_fetch_add(&s->running_work, -1);
            atomic_fetch_add(&s->yield_work, +1);
            log_trace("scheduler yield L:%p, lstate:%p", L, lstate);
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
    atomic_store(&s->running_work, 0);
    atomic_store(&s->yield_work, 0);
    atomic_store(&s->total_work, 0);
    s->stop_flag = false;
    for (int i = 0; i < nprocess; i++) {
        luvco_process_data* pdata = &s->pdata[i];
        pdata->scheduler = s;
    }

    for (int i = 0; i < nprocess; i++) {
        int ret = uv_thread_create(&s->pdata[i].thread, scheduler_thread_cb, (void*)&s->pdata[i]);
        assert(ret == 0);
    }
}

void luvco_scheduler_delete (luvco_scheduler* s) {
    assert(s->stop_flag && "should stop befor delete");
    luvco_ringbuf2_delete(s->worklist);
    free(s->worklist);
}

size_t luvco_scheduler_sizeof (int nprocess) {
    return sizeof(luvco_scheduler) + sizeof(luvco_process_data) * nprocess;
}

int luvco_scheduler_addwork (luvco_scheduler* s, luvco_lstate* l) {
    atomic_fetch_add(&s->total_work, 1);
    atomic_fetch_add(&s->running_work, 1);
    luvco_ringbuf2_spinpush(s->worklist, (void*)l);
    return 0;
}

int luvco_scheduler_resumework (luvco_scheduler* s, luvco_lstate* l) {
    atomic_fetch_add(&s->running_work, 1);
    atomic_fetch_add(&s->yield_work, -1);
    luvco_ringbuf2_spinpush(s->worklist, (void*)l);
    return 0;
}

int luvco_scheduler_totalwork (luvco_scheduler* s) {
    return atomic_load(&s->total_work);
}

void luvco_add_uvwork(luvco_gstate* gstate, luvco_uvwork* uvwork) {
    luvco_ringbuf2_spinpush(gstate->uvworklist, uvwork);
}

void luvco_scheduler_stop (luvco_scheduler* s) {
    s->stop_flag = true;
    for (int i = 0; i < s->nprocess; ++i) {
        uv_thread_join(&s->pdata[i].thread);
    }
}