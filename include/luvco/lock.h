#pragma once
#include <stdatomic.h>

typedef atomic_flag luvco_spinlock;

#define luvco_spinlock_init(spin) atomic_flag_clear(spin)
#define luvco_spinlock_lock(spin) while (atomic_flag_test_and_set(spin)) {}
#define luvco_spinlock_unlock(spin) atomic_flag_clear(spin)