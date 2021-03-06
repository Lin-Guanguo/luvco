#include <luvco.h>
#include <luvco/scheduler.h>

#include <uv.h>

#include <assert.h>
#include <stdlib.h>

typedef struct luvco_process_data {
    uv_thread_t thread;

    // element is luvco_lstate
    // push in eventloop thread
    // pop by this process or steal by other process
    luvco_ringbuf2* worklist;

    struct luvco_process_data* nextprocess;
} luvco_process_data;

typedef struct luvco_scheduler {
    int nprocess;
    luvco_process_data pdata[];
} luvco_scheduler;


#define PROCESS_WORKBUF_LEN 4
#define PROCESS_WORKBUF_FIRST_LEN 8

static luvco_lstate* work_steal (luvco_process_data* data) {
    luvco_process_data* p = data->nextprocess;
    luvco_lstate* work = NULL;
    while (p != data) {
        int ret = luvco_ringbuf2_pop(p->worklist, (void**)&work);
        if (ret == 0) {
            break;
        }
        p = p->nextprocess;
    }
    return work;
}

static void scheduler_thread_cb (void* arg) {
    luvco_process_data* pdata = (luvco_process_data*)arg;
    for (;;) {
        luvco_lstate* lstate;
        lua_State* L;
        int ret = luvco_ringbuf2_pop(pdata->worklist, (void**)&lstate);
        if (ret != 0) {
            lstate = work_steal(pdata);
            if (lstate == NULL) {
                continue;
            }
        }
        int resumeret = 0;
        while ((ret = luvco_ringbuf2_pop(lstate->toresume, (void**)&L)) == 0) {
            log_trace("Thread %p resume L:%p", arg, L);
            resumeret = luvco_resume(L);

            // if all coro end, lua_State has been close, pop from lstate will crash
            if (resumeret == 1) break;
        }

        if (resumeret == 1) {
            log_trace("all coro end lstate:%p, remove from process worklist", lstate);
        } else {
            luvco_ringbuf2_spinpush(pdata->worklist, lstate);
        }
    }
}

void luvco_scheduler_init (luvco_scheduler* s, int nprocess) {
    s->nprocess = nprocess;
    for (int i = 0; i < nprocess; i++) {
        luvco_process_data* pdata = &s->pdata[i];
        pdata->worklist = (luvco_ringbuf2*)malloc(luvco_ringbuf2_sizeof(PROCESS_WORKBUF_LEN));
        luvco_ringbuf2_init(pdata->worklist, PROCESS_WORKBUF_LEN, PROCESS_WORKBUF_FIRST_LEN);
        pdata->nextprocess = &s->pdata[(i+1) % nprocess];
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
    int i = rand() % s->nprocess;
    luvco_process_data* pdata = &s->pdata[i];
    luvco_ringbuf2_spinpush(pdata->worklist, (void*)l);
    return 0;
}
