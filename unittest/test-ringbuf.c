#include <luvco.h>
#include <luvco/tools.h>

#include <unity.h>

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

long long push_failed_count;
long long pop_failed_count;
bool push_over = false;
bool pop_over = false;

void* push_thread (void* buf) {
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

void* pop_thread (void* buf) {
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
    pthread_t t1;
    pthread_t t2;
    pthread_create(&t1, NULL, push_thread, (void*)r);
    pthread_create(&t2, NULL, pop_thread, (void*)r);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    luvco_ringbuf2_delete(r);
    free(r);
    TEST_ASSERT_TRUE(push_over);
    TEST_ASSERT_TRUE(pop_over);
    // printf("push failed count = %lld\n", push_failed_count);
    // printf("pop failed count = %lld\n", pop_failed_count);
}