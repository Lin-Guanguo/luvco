#pragma once
#include <luvco.h>
#include <luvco/log.h>

#include <lua/lua.h>
#include <lua/lauxlib.h>

#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>

extern const char* LUVCO_UDATAMETA_SIZEOF_FIELD;

#define luvco_new_meta(L, type) \
    luaL_newmetatable((L), "luvco."#type); \
    lua_pushvalue((L), -1); \
    lua_setfield((L), -2, "__index"); \
    lua_pushinteger((L), sizeof(type)); \
    lua_setfield(L, -2, LUVCO_UDATAMETA_SIZEOF_FIELD)

#define luvco_pushudata_with_meta(L, type) \
    (type*)lua_newuserdatauv((L), sizeof(type), 0); \
    luaL_setmetatable((L), "luvco."#type)

#define luvco_check_udata(L, n, type) \
    (type*)luaL_checkudata((L), (n), "luvco."#type)

#define container_of(ptr, type, member) (type*)((char*)(ptr) - (char*)(&(((type*)NULL)->member)))

typedef struct luvco_gstate {
    uv_loop_t loop;
    lua_State* main_coro;
    luvco_newluaf newluaf;
    void* newluaf_ud;
} luvco_gstate;

typedef struct luvco_ringbuf2 luvco_ringbuf2;

typedef struct luvco_lstate {
    // only push by eventloop thread.
    // elements is lua_State*.
    luvco_ringbuf2* toresume;
} luvco_lstate;

luvco_gstate* luvco_get_gstate (lua_State* L);

luvco_lstate* luvco_get_lstate (lua_State* L);

#define luvco_pyield(handle, L, ctx, kf) \
    (handle)->L = (L); \
    luvco_yield((L), (lua_KContext)(ctx), (kf))

void luvco_yield (lua_State *L, lua_KContext k_ctx, lua_KFunction k);

void luvco_resume (lua_State *L, int nargs);

void luvco_dump_lua_stack (lua_State *L);


typedef atomic_flag luvco_spinlock;

#define luvco_spinlock_lock(spin) while (atomic_flag_test_and_set(spin)) {}
#define luvco_spinlock_unlock(spin) atomic_flag_clear(spin)

typedef struct luvco_ringbuf {
    int len;
    volatile int head;
    volatile int tail;
    void* volatile ring[];
} luvco_ringbuf;

void luvco_ringbuf_init (luvco_ringbuf* r, int len);

int luvco_ringbuf_push (luvco_ringbuf* r, void* data);

int luvco_ringbuf_pop (luvco_ringbuf* r, void** data);

typedef struct luvco_ringbuf2 {
    int len;
    volatile int head;
    volatile int tail;
    luvco_ringbuf* volatile ring[];
} luvco_ringbuf2;

void luvco_ringbuf2_init (luvco_ringbuf2* r, int len, int firstbufsize);

int luvco_ringbuf2_push (luvco_ringbuf2* r, void* data);

int luvco_ringbuf2_pop (luvco_ringbuf2* r, void** data);

int luvco_ringbuf2_delete (luvco_ringbuf2* r);



typedef struct luvco_objhead {
    bool moved;
} luvco_objhead;

#define luvco_init_objheader(head) (head)->moved = false;

#define luvco_checkmoved(L, head) \
    if ((head)->moved) { \
        log_error("luvco obj %p is moved", (head)); \
        luaL_error(L, "luvco obj %p is moved", (head)); \
    }
