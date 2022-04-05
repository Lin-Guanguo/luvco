#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>

#include <uv.h>
#include <luvco.h>
#include <luvco/tools.h>
#include <stdlib.h>
#include <string.h>

static const char* CORO_TABLE_FIELD = "luvco.global_corotable";
static int CORO_TABLE_COUNT_IDX = 1;
static const char* GSTATE_FIELD = "luvco.global_state";
static const char* LSTATE_FIELD = "luvco.local_state";

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

static void unregister_coro (lua_State* L) {
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
        log_trace("all coro end, close state %p", L);
        lua_close(L);
    }
}

// lua_state has only one shared luvco_gstate
static void luvco_init_luastate (lua_State* L, luvco_gstate* state, bool close_lua) {
    luaL_requiref(L, "luvco", luvco_open_base, 1);
    lua_pop(L, 1);

    lua_pushlightuserdata(L, state);
    lua_setfield(L, LUA_REGISTRYINDEX, GSTATE_FIELD);

    lua_newtable(L);
    // if close_lua is false, init coro counter as 2, prevent lua_close
    lua_pushinteger(L, close_lua ? 1 : 2);
    lua_seti(L, -2, CORO_TABLE_COUNT_IDX);
    lua_setfield(L, LUA_REGISTRYINDEX, CORO_TABLE_FIELD);
}

luvco_gstate* luvco_get_gstate (lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, GSTATE_FIELD);
    luvco_gstate* state = (luvco_gstate*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return state;
}

void luvco_yield (lua_State *L, lua_KContext k_ctx, lua_KFunction k) {
    // no use the yield result
    lua_yieldk(L, 0, k_ctx, k);
}

void luvco_resume(lua_State *L, int nargs) {
    int res;
    int ret = lua_resume(L, NULL, nargs, &res);
    if (res != 0) {
        log_error("unexpected lua resume return");
        luvco_dump_lua_stack(L);
    }
    switch (ret) {
        case LUA_YIELD: break;
        case LUA_OK: unregister_coro(L); break;
        default:
            log_error("luvco resume error");
            luvco_dump_lua_stack(L);
    }
}

static int spawn (lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_State* NL = lua_newthread(L);
    register_coro(L);

    lua_pushvalue(L, 1); // copy function to top
    lua_xmove(L, NL, 1);  // pop function from L to NL

    log_trace("spawn from L:%p, NL:%p", L, NL);

    luvco_resume(NL, 0);
    return 0;
}

static int ispawn (lua_State *L) {
    size_t str_len;
    const char* code = luaL_checklstring(L, 1, &str_len);

    luvco_gstate* state = luvco_get_gstate(L);
    lua_State* NL = (state->newluaf)(state->newluaf_ud);
    log_trace("ispawn from L:%p, NL:%p", L, NL);

    luaL_loadbuffer(NL, code, str_len, "NL");
    luvco_init_luastate(NL, state, true);
    luvco_resume(NL, 0);
    return 0;
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

    luvco_init_luastate(L, state, false);
    return state;
}

void luvco_run (luvco_gstate* state) {
    log_trace("luvco run in State:%p", state);
    luvco_resume(state->main_coro, 0);
    uv_run(&state->loop, UV_RUN_DEFAULT);
    log_trace("luvco end in State:%p", state);
}

void luvco_close (luvco_gstate* state) {
    uv_loop_close(&state->loop);
    free(state);
}
