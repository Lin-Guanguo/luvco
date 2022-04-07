#include <luvco/tools.h>

#include <assert.h>
#include <stdlib.h>

#define RINGBUF2_FACTOR 2

typedef struct luvco_ringbuf {
    int len;
    volatile int head;
    volatile int tail;
    luvco_spinlock pushlock;
    luvco_spinlock poplock;
    void* volatile ring[];
} luvco_ringbuf;

typedef struct luvco_ringbuf2 {
    int len;
    volatile int head;
    volatile int tail;
    luvco_spinlock pushlock;
    luvco_spinlock poplock;
    luvco_ringbuf* volatile ring[];
} luvco_ringbuf2;

size_t luvco_ringbuf_sizeof (int len) {
    return sizeof(luvco_ringbuf) + sizeof(void*) * len;
}

void luvco_ringbuf_init (luvco_ringbuf* r, int len) {
    r->len = len;
    r->head = 0;
    r->tail = 0;
    luvco_spinlock_init(&r->pushlock);
    luvco_spinlock_init(&r->poplock);
}

int luvco_ringbuf_unlockpush (luvco_ringbuf* r, void* data) {
    int tail = r->tail;
    int nexttail = (tail + 1) % r->len;
    if (nexttail == r->head) {
        return -1;
    }
    r->ring[tail] = data;
    r->tail = nexttail;
    return 0;
}

int luvco_ringbuf_push (luvco_ringbuf* r, void* data) {
    luvco_spinlock_lock(&r->pushlock);
    int ret = luvco_ringbuf_unlockpush(r, data);
    luvco_spinlock_unlock(&r->pushlock);
    return ret;
}

int luvco_ringbuf_unlockpop (luvco_ringbuf* r, void** data) {
    int head = r->head;
    if (head == r->tail) {
        return -1;
    }
    *data = r->ring[head];
    r->head = (head + 1) % r->len;
    return 0;

}

int luvco_ringbuf_pop (luvco_ringbuf* r, void** data) {
    luvco_spinlock_lock(&r->poplock);
    int ret = luvco_ringbuf_unlockpop(r, data);
    luvco_spinlock_unlock(&r->poplock);
    return ret;
}

size_t luvco_ringbuf2_sizeof (int len) {
    return sizeof(luvco_ringbuf2) + sizeof(void*) * len;
}

void luvco_ringbuf2_init (luvco_ringbuf2* r, int len, int firstbufsize) {
    r->len = len;
    r->head = 0;
    r->tail = 1;
    r->ring[0] = (luvco_ringbuf*)malloc(sizeof(luvco_ringbuf) + sizeof(void*) * firstbufsize);
    luvco_ringbuf_init(r->ring[0], firstbufsize);
    luvco_spinlock_init(&r->pushlock);
    luvco_spinlock_init(&r->poplock);
}

int luvco_ringbuf2_unlockpush (luvco_ringbuf2* r, void* data) {
    int len = r->len;
    int tail = r->tail;
    int last = (tail == 0) ? (len - 1) : (tail - 1);
    luvco_ringbuf* lastbuf = r->ring[last];
    int ret = luvco_ringbuf_unlockpush(lastbuf, data);
    if (ret == 0) {
        return 0;
    }
    int nexttail = (tail + 1) % len;
    int head = r->head;
    if (nexttail == head) {
        return -1;
    }
    int lastlen = lastbuf->len;
    int newlen = lastlen * RINGBUF2_FACTOR;
    lastbuf = r->ring[tail] = (luvco_ringbuf*)malloc(sizeof(luvco_ringbuf) + sizeof(luvco_ringbuf) * newlen);;
    luvco_ringbuf_init(lastbuf, newlen);
    ret = luvco_ringbuf_unlockpush(lastbuf, data);
    assert(ret == 0);
    r->tail = nexttail;
    return 0;
}

int luvco_ringbuf2_push (luvco_ringbuf2* r, void* data) {
    luvco_spinlock_lock(&r->pushlock);
    int ret = luvco_ringbuf2_unlockpush(r, data);
    luvco_spinlock_unlock(&r->pushlock);
    return ret;
}

int luvco_ringbuf2_unlockpop (luvco_ringbuf2* r, void** data) {
    int head = r->head;
    luvco_ringbuf* headbuf = r->ring[head];
    int ret = luvco_ringbuf_unlockpop(headbuf, data);
    if (ret == 0) {
        return 0;
    }
    int len = r->len;
    int nexthead = (head + 1) % len;
    int tail = r->tail;
    if (nexthead == tail) {
        return -1;
    }
    free(headbuf);
    r->head = head = nexthead;
    headbuf = r->ring[head];
    ret = luvco_ringbuf_unlockpop(headbuf, data);
    assert(ret == 0);
    return 0;
}

int luvco_ringbuf2_pop (luvco_ringbuf2* r, void** data) {
    luvco_spinlock_lock(&r->poplock);
    int ret = luvco_ringbuf2_unlockpop(r, data);
    luvco_spinlock_unlock(&r->poplock);
    return ret;
}

int luvco_ringbuf2_delete (luvco_ringbuf2* r) {
    int head = r->head;
    int tail = r->tail;
    for (int i = head; i < tail; i++) {
        free(r->ring[i]);
    }
    return 0;
}

