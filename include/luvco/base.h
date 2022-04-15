#pragma once
#include <luvco.h>
#include <luvco/log.h>

#include <lua/lua.h>
#include <uv.h>

typedef void (*luvco_cb_f) (void*);
typedef struct luvco_ringbuf2 luvco_ringbuf2;
typedef struct luvco_scheduler luvco_scheduler;

typedef struct luvco_uvwork luvco_uvwork;
typedef void (*luvco_uvwork_cb) (luvco_uvwork* work);
typedef struct luvco_uvwork {
    luvco_uvwork_cb cb;
} luvco_uvwork;

typedef struct luvco_gstate {
    uv_loop_t loop;
    lua_State* main_coro;
    luvco_newluaf newluaf;
    void* newluaf_ud;

    luvco_scheduler* scheduler;

    luvco_ringbuf2* uvworklist; // element si luvco_uvwork*, only pop by eventloop thread
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

void luvco_yield_thread (lua_State *L, lua_KContext k_ctx, lua_KFunction k, luvco_cb_f run_after_yield, void* f_ud);

enum luvco_resume_return {
    LUVCO_RESUME_NORMAL = 0,
    LUVCO_RESUME_LSTATE_END,
    LUVCO_RESUME_YIELD_THREAD,
    LUVCO_RESUME_ERROR,
};

// must run after f if it is not NULL
enum luvco_resume_return luvco_resume (lua_State_flag* L2);
