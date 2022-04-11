#include <luvco.h>
#include <luvco/tools.h>
#include <luvco/object.h>
#include <luvco/scheduler.h>

#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <uv.h>

#include <stdlib.h>
#include <assert.h>
#include <string.h>

static const char* CORO_TABLE_FIELD = "luvco.global_corotable";
static int CORO_TABLE_COUNT_IDX = 1;
static const char* GSTATE_FIELD = "luvco.global_state";
static const char* LSTATE_FIELD = "luvco.local_state";
static const char* MAINSTATE_TAG_FILED = "luvco.main_state_tag";

static int RESUME_QUEUE_SIZE = 8;
static int RESUME_QUEUE_BUF_SIZE = 8;

static void local_state_init (luvco_lstate* lstate) {
    log_trace("local state init %p", lstate);
    lstate->toresume = (luvco_ringbuf2*)malloc(luvco_ringbuf2_sizeof(RESUME_QUEUE_SIZE));
    luvco_ringbuf2_init(lstate->toresume, RESUME_QUEUE_SIZE, RESUME_QUEUE_BUF_SIZE);
}

static void local_state_delete (luvco_lstate* lstate) {
    log_trace("local state delete %p", lstate);
    luvco_ringbuf2_delete(lstate->toresume);
    free(lstate->toresume);
}

// top of stack is coroutine thread.
// prevent gc collect coroutine
//
// regiter coro to global core table, key is lua_State udata
// value is coroutine
static void register_coro (lua_State* L) {
    luaL_checktype(L, -1, LUA_TTHREAD);                     // thread
    lua_getfield(L, LUA_REGISTRYINDEX, CORO_TABLE_FIELD);    // coro_table
    lua_pushlightuserdata(L, (void*)L);                     // udata
    lua_pushvalue(L, -3);                                   // thread
    lua_settable(L, -3);                                    // set coro_table, pop 2: udata, thread

    lua_geti(L, -1, CORO_TABLE_COUNT_IDX);                // count
    int count = lua_tointeger(L, -1);
    lua_pop(L, 1);                                          // pop count
    lua_pushinteger(L, count+1);                            // push count + 1
    lua_seti(L, -2, CORO_TABLE_COUNT_IDX);                // update count
    lua_pop(L, 1);                                          // pop coro_table
}

// return 1 if all coro end
static int unregister_coro (lua_State* L) {
    log_trace("unregister coro %p", L);
    lua_getfield(L, LUA_REGISTRYINDEX, CORO_TABLE_FIELD);
    lua_pushlightuserdata(L, (void*)L);
    lua_pushnil(L);
    lua_settable(L, -3);

    lua_geti(L, -1, CORO_TABLE_COUNT_IDX);
    int count = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pushinteger(L, count-1);
    lua_seti(L, -2, CORO_TABLE_COUNT_IDX);
    if (count - 1 == 0) {
        return 1;
    }
    return 0;
}

// lua_state has only one shared luvco_gstate
static void luvco_init_luastate (lua_State* L, luvco_gstate* state) {
    luaL_requiref(L, "luvco", luvco_open_base, 1);
    lua_pop(L, 1);

    lua_pushlightuserdata(L, state);
    lua_setfield(L, LUA_REGISTRYINDEX, GSTATE_FIELD);
    luvco_lstate* lstate = lua_newuserdatauv(L, sizeof(luvco_lstate), 0);
    lua_setfield(L, LUA_REGISTRYINDEX, LSTATE_FIELD);
    local_state_init(lstate);

    lua_newtable(L);
    lua_pushinteger(L, 1);
    lua_seti(L, -2, CORO_TABLE_COUNT_IDX);
    lua_setfield(L, LUA_REGISTRYINDEX, CORO_TABLE_FIELD);
}

luvco_gstate* luvco_get_gstate (lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, GSTATE_FIELD);
    luvco_gstate* state = (luvco_gstate*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return state;
}

luvco_lstate* luvco_get_lstate (lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, LSTATE_FIELD);
    luvco_lstate* state = (luvco_lstate*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return state;
}

void luvco_yield (lua_State *L, lua_KContext k_ctx, lua_KFunction k) {
    // no use the yield result
    lua_yieldk(L, 0, k_ctx, k);
}

enum luvco_special_yield_tag {
    LUVCO_YIELD_THREAD = 1086,
};

void luvco_yield_thread (lua_State *L, lua_KContext k_ctx, lua_KFunction k, luvco_after_yield_f f, void* ud) {
    lua_pushlightuserdata(L, f);
    lua_pushlightuserdata(L, ud);
    lua_pushlightuserdata(L, (void*)LUVCO_YIELD_THREAD);
    lua_yieldk(L, 3, k_ctx, k);
}

void luvco_toresume (luvco_lstate* lstate, lua_State* L, int nargs) {
    assert(nargs >= 0 && nargs <= 3 && "args should between 0 and 3");
    assert(((intptr_t)L & 3) == 0 && "lua state not align, can't use low bit as flag");

    L = (lua_State*)((intptr_t)L | nargs);
    luvco_ringbuf2_spinpush(lstate->toresume, L);
}

enum luvco_resume_return luvco_resume (lua_State_flag* Lb) {
    int nargs = (intptr_t)Lb & 3;
    lua_State* L = (lua_State*)((intptr_t)Lb & (~3));

    int res;
    int ret = lua_resume(L, NULL, nargs, &res);
    luvco_after_yield_f afterf;
    if (ret == LUA_YIELD && res != 0) {
        intptr_t tag = (intptr_t)lua_touserdata(L, -1);
        switch (tag) {
        case LUVCO_YIELD_THREAD:
            afterf = lua_touserdata(L, 1);
            if (afterf != NULL) {
                void* afterf_ud = lua_touserdata(L, 2);
                afterf(afterf_ud);
            }
            return LUVCO_RESUME_YIELD_THREAD;
        default:
            goto res_error;
        }
    } else if (res != 0) {
        res_error:
        log_error("unexpected lua resume return");
        luvco_dump_lua_stack(L);
        return LUVCO_RESUME_ERROR;
    }

    switch (ret) {
    case LUA_YIELD:
        return LUVCO_RESUME_NORMAL;
    case LUA_OK:
        ret = unregister_coro(L);
        if (ret != 1) {
            return LUVCO_RESUME_NORMAL;
        }
        int ty = lua_getfield(L, LUA_REGISTRYINDEX, MAINSTATE_TAG_FILED);
        lua_pop(L, 1);
        if (ty == LUA_TNIL) {
            log_trace("all coro end, close state %p", L);
            luvco_lstate* lstate = luvco_get_lstate(L);
            local_state_delete(lstate);
            lua_close(L);
        } else {
            // main lua_State not close, just gc collect object
            lua_gc(L, LUA_GCCOLLECT);
        }
        return LUVCO_RESUME_LSTATE_END;
    default:
        log_error("luvco resume error");
        luvco_dump_lua_stack(L);
        return LUVCO_RESUME_ERROR;
    }
    assert(false && "error");
}

static int spawn_k (lua_State *L, int status, lua_KContext ctx) {
    return 0;
}

static int spawn (lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_State* NL = lua_newthread(L);
    register_coro(L);

    lua_pushvalue(L, 1); // copy function to top
    lua_xmove(L, NL, 1);  // pop function from L to NL

    log_trace("spawn from L:%p, NL:%p", L, NL);

    luvco_lstate* lstate = luvco_get_lstate(L);
    luvco_toresume(lstate, L, 0);
    luvco_toresume(lstate, NL, 0);
    luvco_yield(L, (lua_KContext)NULL, spawn_k);
}

static int ispawn_k (lua_State *L, int status, lua_KContext ctx) {
    return 0;
}

static int ispawn (lua_State *L) {
    size_t str_len;
    const char* code = luaL_checklstring(L, 1, &str_len);

    luvco_gstate* state = luvco_get_gstate(L);
    lua_State* NL = (state->newluaf)(state->newluaf_ud);

    luaL_loadbuffer(NL, code, str_len, "NL");
    luvco_init_luastate(NL, state);
    luvco_lstate* lstate = luvco_get_lstate(L);
    luvco_lstate* nlstate = luvco_get_lstate(NL);
    log_trace("ispawn from lstate:%p, nlstate:%p", lstate, nlstate);

    luvco_toresume(lstate, L, 0);
    luvco_toresume(nlstate, NL, 0);
    luvco_scheduler_addwork(state->scheduler, nlstate);
    luvco_yield(L, (lua_KContext)NULL, ispawn_k);
}

static void tmp_yield_cb (uv_idle_t* handle) {

}

static int tmp_yield (lua_State *L) {
    return 0;
}

static int import_lib (lua_State *L) {
    const char* s = luaL_checkstring(L, 1);
    if (strcmp(s, "lualibs") == 0) {
        luaL_openlibs(L);
        return 0;
    } else if (strcmp(s, "net") == 0) {
        luaL_requiref(L, "luvco_net", luvco_open_net, 0);
    }
    return 1;
}

static const luaL_Reg base_lib [] = {
    { "spawn", spawn },
    { "ispawn", ispawn },
    { "import", import_lib },
    { NULL, NULL }
};

int luvco_open_base (lua_State* L) {
    luaL_newlib(L, base_lib);
    return 1;
}

static lua_State* default_newluaf (void* ud) {
    return luaL_newstate();
}

luvco_gstate* luvco_init (lua_State* L, luvco_newluaf f, void* f_ud) {
    if (f == NULL) {
        f = default_newluaf;
    }

    log_trace("luvco init main_coro L:%p", L);

    luvco_gstate* state = malloc(sizeof(luvco_gstate));
    state->main_coro = L;
    state->newluaf = f;
    state->newluaf_ud = f_ud;
    uv_loop_init(&state->loop);

    luvco_init_luastate(L, state);
    lua_pushinteger(L, 0);
    lua_setfield(L, LUA_REGISTRYINDEX, MAINSTATE_TAG_FILED);

    return state;
}

#define NPROCESS 4

static void prevent_quit (uv_idle_t* handle) {
    // TODO: quit when all lua_State execute over
}

void luvco_run (luvco_gstate* state) {
    log_trace("luvco run, global state:%p", state);

    luvco_scheduler* s = (luvco_scheduler*)malloc(luvco_scheduler_sizeof(NPROCESS));
    luvco_scheduler_init(s, NPROCESS);
    state->scheduler = s;
    luvco_lstate* lstate = luvco_get_lstate(state->main_coro);
    luvco_toresume(lstate, state->main_coro, 0);
    luvco_scheduler_addwork(s, lstate);

    uv_idle_t idle;
    uv_idle_init(&state->loop, &idle);
    uv_idle_start(&idle, prevent_quit);
    uv_run(&state->loop, UV_RUN_DEFAULT);

    log_trace("luvco end, global state:%p", state);
}

void luvco_close (luvco_gstate* state) {
    uv_loop_close(&state->loop);

    // clean main lua state
    lua_State* L = state->main_coro;
    luvco_lstate* lstate = luvco_get_lstate(L);
    local_state_delete(lstate);
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, GSTATE_FIELD);
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, LSTATE_FIELD);
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, CORO_TABLE_FIELD);

    free(state);
}
