#include <unity.h>

void setUp (void) {} /* Is run before every test, put unit init calls here. */
void tearDown (void) {} /* Is run after every test, put unit clean-up calls here. */

void test_ringbuf ();

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ringbuf);
    return UNITY_END();
}