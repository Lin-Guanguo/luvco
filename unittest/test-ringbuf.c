#include <luvco.h>
#include <luvco/tools.h>

#include <uv.h>
#include <unity.h>

#include <stdio.h>
#include <stdlib.h>

static long long push_failed_count;
static long long pop_failed_count;
static bool push_over = false;
static bool pop_over = false;

static void push_thread (void* buf) {
    luvco_ringbuf2* r = (luvco_ringbuf2*)buf;
    for (long long i = 0; i < 10000; ++i) {
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

static void pop_thread (void* buf) {
    luvco_ringbuf2* r = (luvco_ringbuf2*)buf;
    for (long long i = 0; i < 10000; ++i) {
        long long data;
        bool failed = false;
        while (luvco_ringbuf2_pop(r, (void**)&data) != 0) {
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
    luvco_ringbuf2* r = (luvco_ringbuf2*)malloc(sizeof(luvco_ringbuf2) + sizeof(void*) * 8);
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