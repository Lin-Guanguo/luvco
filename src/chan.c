#include <luvco/tools.h>
#include <stdlib.h>
#include <stdatomic.h>j

typedef struct luvco_chan {
    void** loop_buf;
    int len;
    volatile int head;
    volatile int tail;

    atomic_char owner;
} luvco_chan;

int luvco_chan_init (luvco_chan* ch, int len) {
    ch->loop_buf = (void**)malloc(sizeof(void*) * (len+1));
    ch->len = len + 1; // reserve one to check buf is full
    ch->head = 0;
    ch->tail = 0;
    atomic_init(&ch->owner, 2);
}

// chan has a sender and receiver, is drop twice, the chan memory will free
int luvco_chan_drop (luvco_chan* ch) {
    char i = atomic_fetch_sub(&ch->owner, 1);
    assert (i >= 0);
    if (i == 0) {
        free(ch);
    }
}

int luvco_chan_push (luvco_chan* ch, void* data) {
    int old_tail = ch->tail;
    int next_tail = (old_tail + 1) % ch->len;
    if (next_tail == ch->head) {
        return -1;
    } else {
        ch->loop_buf[old_tail] = data;
        ch->tail = next_tail;
        return 0;
    }
}

int luvco_chan_pop (luvco_chan* ch, void** data) {
    int old_head = ch->head;
    if (old_head == ch->tail) {
        return -1;
    } else {
        *data = ch->loop_buf[old_head];
        ch->head = (old_head + 1) % ch->len;
        return 0;
    }
}
