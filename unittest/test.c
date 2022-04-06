#include <unity.h>

void setUp (void) {} /* Is run before every test, put unit init calls here. */
void tearDown (void) {} /* Is run after every test, put unit clean-up calls here. */

void test_ringbuf ();
void test_ringbuf_withlock ();
void test_spinlock ();

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ringbuf);
    RUN_TEST(test_ringbuf_withlock);
    RUN_TEST(test_spinlock);
    return UNITY_END();
}