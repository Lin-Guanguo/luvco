#pragma once
#include <luvco.h>
#include <luvco/log.h>

#include <lua/lua.h>
#include <lua/lauxlib.h>

#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>

#define container_of(ptr, type, member) (type*)((char*)(ptr) - (char*)(&(((type*)NULL)->member)))

typedef struct luvco_ringbuf luvco_ringbuf;
typedef struct luvco_ringbuf2 luvco_ringbuf2;
typedef struct luvco_scheduler luvco_scheduler;

typedef struct luvco_gstate {
    uv_loop_t loop;
    lua_State* main_coro;
    luvco_newluaf newluaf;
    void* newluaf_ud;
    luvco_scheduler* scheduler;
} luvco_gstate;

typedef struct luvco_lstate {
    luvco_ringbuf2* toresume;
} luvco_lstate;

typedef lua_State lua_State_flag; // use pointer'2 lower 2 bit as flag

void luvco_dump_lua_stack (lua_State *L);
luvco_gstate* luvco_get_gstate (lua_State* L);
luvco_lstate* luvco_get_lstate (lua_State* L);
void luvco_yield (lua_State *L, lua_KContext k_ctx, lua_KFunction k);
void luvco_toresume (luvco_lstate* lstate, lua_State *L, int nargs);

typedef void (*luvco_after_yield_f) (void*);
void luvco_yield_thread (lua_State *L, lua_KContext k_ctx, lua_KFunction k, luvco_after_yield_f f, void* ud);

enum luvco_resume_return {
    LUVCO_RESUME_NORMAL = 0,
    LUVCO_RESUME_LSTATE_END,
    LUVCO_RESUME_YIELD_THREAD,
    LUVCO_RESUME_ERROR,
};

// must run after f if it is not NULL
enum luvco_resume_return luvco_resume (lua_State_flag* L2);


typedef atomic_flag luvco_spinlock;

#define luvco_spinlock_init(spin) atomic_flag_clear(spin)
#define luvco_spinlock_lock(spin) while (atomic_flag_test_and_set(spin)) {}
#define luvco_spinlock_unlock(spin) atomic_flag_clear(spin)


size_t luvco_ringbuf_sizeof (int len);
void luvco_ringbuf_init (luvco_ringbuf* r, int len);
int luvco_ringbuf_push (luvco_ringbuf* r, void* data);
#define luvco_ringbuf_spinpush(r, data) while (luvco_ringbuf_push((r), (data)) != 0);
int luvco_ringbuf_unlockpush (luvco_ringbuf* r, void* data);
int luvco_ringbuf_pop (luvco_ringbuf* r, void** data);
int luvco_ringbuf_unlockpop (luvco_ringbuf* r, void** data);


size_t luvco_ringbuf2_sizeof (int len);
void luvco_ringbuf2_init (luvco_ringbuf2* r, int len, int firstbufsize);
int luvco_ringbuf2_push (luvco_ringbuf2* r, void* data);
#define luvco_ringbuf2_spinpush(r, data) while (luvco_ringbuf2_push((r), (data)) != 0);
int luvco_ringbuf2_unlockpush (luvco_ringbuf2* r, void* data);
int luvco_ringbuf2_pop (luvco_ringbuf2* r, void** data);
int luvco_ringbuf2_unlockpop (luvco_ringbuf2* r, void** data);
int luvco_ringbuf2_delete (luvco_ringbuf2* r);


void luvco_open_chan_withbase (lua_State* L);
void luvco_chan1_cross_state (lua_State *Lsend, lua_State *Lrecv);