#include <luvco/tools.h>

#include <uv.h>

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

void luvco_scheduler_init (luvco_scheduler* s, int nprocess);

size_t luvco_scheduler_sizeof (int nprocess);

int luvco_scheduler_addwork (luvco_scheduler* s, luvco_lstate* l);