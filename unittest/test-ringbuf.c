#include <luvco.h>
#include <luvco/luvco.h>

#include <uv.h>
#include <unity.h>

#include <stdio.h>
#include <stdlib.h>

static long long push_failed_count;
static long long pop_failed_count;
static bool push_over = false;
static bool pop_over = false;

#define NLOOP 10000
#define NTHREAD 10

static void push_thread (void* buf) {
    luvco_ringbuf2* r = (luvco_ringbuf2*)buf;
    for (long long i = 0; i < NLOOP * NTHREAD / 2; ++i) {
        bool failed = false;
        while (luvco_ringbuf2_unlockpush(r, (void*)i) != 0) {
            if (!failed) {
                push_failed_count++;
                failed = true;
            }
        }
    }
    push_over = true;
}

static void pop_thread (void* buf) {
    luvco_ringbuf2* r = (luvco_ringbuf2*)buf;
    for (long long i = 0; i < NLOOP * NTHREAD / 2; ++i) {
        long long data;
        bool failed = false;
        while (luvco_ringbuf2_unlockpop(r, (void**)&data) != 0) {
            if (!failed) {
                pop_failed_count++;
                failed = true;
            }
        }
        TEST_ASSERT_EQUAL_INT64(data, i);
    }
    pop_over = true;
}

void test_ringbuf () {
    luvco_ringbuf2* r = (luvco_ringbuf2*)malloc(luvco_ringbuf2_sizeof(8));
    luvco_ringbuf2_init(r, 8, 8);

    uv_thread_t t1;
    uv_thread_t t2;
    uv_thread_create(&t1, push_thread, (void*)r);
    uv_thread_create(&t1, pop_thread, (void*)r);
    uv_thread_join(&t1);
    uv_thread_join(&t2);

    luvco_ringbuf2_delete(r);
    free(r);
    TEST_ASSERT_TRUE(push_over);
    TEST_ASSERT_TRUE(pop_over);
    // printf("push failed count = %lld\n", push_failed_count);
    // printf("pop failed count = %lld\n", pop_failed_count);
}

static long long count = 0;
static luvco_spinlock count_lock;

static void push_thread2 (void* buf) {
    luvco_ringbuf2* r = (luvco_ringbuf2*)buf;
    for (long long i = 0; i < NLOOP; ++i) {
        bool failed = false;
        while (luvco_ringbuf2_push(r, (void*)i) != 0) {
            if (!failed) {
                push_failed_count++;
                failed = true;
            }
        }
    }
    push_over = true;
}

static void pop_thread2 (void* buf) {
    luvco_ringbuf2* r = (luvco_ringbuf2*)buf;
    for (long long i = 0; i < NLOOP; ++i) {
        long long data;
        bool failed = false;
        while (luvco_ringbuf2_pop(r, (void**)&data) != 0) {
            if (!failed) {
                pop_failed_count++;
                failed = true;
            }
        }
    }
    luvco_spinlock_lock(&count_lock);
    count += NLOOP;
    luvco_spinlock_unlock(&count_lock);
}

void test_ringbuf_withlock () {
    luvco_ringbuf2* r = (luvco_ringbuf2*)malloc(luvco_ringbuf2_sizeof(8));
    luvco_ringbuf2_init(r, 8, 8);

    uv_thread_t thread[NTHREAD];
    for (int i = 0; i < NTHREAD; i++) {
        if (i < NTHREAD / 2) {
            uv_thread_create(&thread[i], push_thread2, (void*)r);
        } else {
            uv_thread_create(&thread[i], pop_thread2, (void*)r);
        }
    }
    for (int i = 0; i < NTHREAD; i++) {
        uv_thread_join(&thread[i]);
    }

    luvco_ringbuf2_delete(r);
    free(r);
    TEST_ASSERT_EQUAL_INT64(NLOOP * NTHREAD / 2, count);
}