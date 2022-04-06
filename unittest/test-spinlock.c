#include <luvco.h>
#include <luvco/tools.h>

#include <uv.h>
#include <unity.h>

static luvco_spinlock lock;
static long long count = 0;

#define NTHREAD  10
#define NLOOP  10000

static void addcount (void* a) {
    for (int i = 0; i < NLOOP; i++) {
        luvco_spinlock_lock(&lock);
        count++;
        luvco_spinlock_unlock(&lock);
    }

};

void test_spinlock () {
    luvco_spinlock_init(&lock);

    uv_thread_t thread[NTHREAD];
    for (int i = 0; i < NTHREAD; i++) {
        uv_thread_create(&thread[i], addcount, NULL);
    }
    for (int i = 0; i < NTHREAD; i++) {
        uv_thread_join(&thread[i]);
    }
    TEST_ASSERT_EQUAL_INT64(NTHREAD * NLOOP, count);
}