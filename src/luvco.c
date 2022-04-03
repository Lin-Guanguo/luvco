#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>

#include <uv.h>
#include <luvco.h>
#include <luvco/tools.h>

static const char* coro_table_name = "luvco.global_corotable";
static int coro_table_count_index = 1;

// top of stack is coroutine thread.
// prevent gc collect coroutine
//
// regiter coro to global core table, key is lua_State udata
// value is coroutine
static void register_coro (lua_State* L) {
    luaL_checktype(L, -1, LUA_TTHREAD);                     // thread
    lua_getfield(L, LUA_REGISTRYINDEX, coro_table_name);    // coro_table
    lua_pushlightuserdata(L, (void*)L);                     // udata
    lua_pushvalue(L, -3);                                   // thread
    lua_settable(L, -3);                                    // set coro_table, pop 2: udata, thread

    lua_geti(L, -1, coro_table_count_index);                // count
    int count = lua_tointeger(L, -1);
    lua_pop(L, 1);                                          // pop count
    lua_pushinteger(L, count+1);                            // push count + 1
    lua_seti(L, -2, coro_table_count_index);                // update count
    lua_pop(L, 1);                                          // pop coro_table
}

static void unregister_coro (lua_State* L) {
    log_trace("unregister coro %p", L);
    lua_getfield(L, LUA_REGISTRYINDEX, coro_table_name);
    lua_pushlightuserdata(L, (void*)L);
    lua_pushnil(L);
    lua_settable(L, -3);

    lua_geti(L, -1, coro_table_count_index);
    int count = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pushinteger(L, count-1);
    lua_seti(L, -2, coro_table_count_index);
    if (count - 1 == 0) {
        log_trace("all coro end, close state %p", L);
        lua_close(L);
    }
}

// lua_state has only one shared luvco_state
static luvco_state* luvco_init_state (lua_State* L) {
    luvco_new_meta(L, luvco_state);
    lua_pop(L, 1);
    luvco_state* state = luvco_pushudata_with_meta(L, luvco_state);
    uv_loop_init(&state->loop);
    lua_setfield(L, LUA_REGISTRYINDEX, "luvco.global_state");
    return state;
}

luvco_state* luvco_get_state (lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "luvco.global_state");
    luvco_state* state = (luvco_state*)lua_touserdata(L, -1);
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
    luaL_checkstring(L, 1);
    size_t str_len;
    const char* code = luaL_tolstring(L, 1, &str_len);

    // TODO: pass alloc function;
    lua_State* NL = luaL_newstate();
    // TODO: pass libs
    luaL_openlibs(NL);
    luaL_requiref(NL, "luvco", luvco_open_base, 1);
    luaL_requiref(NL, "luvco_net", luvco_open_net, 1);
    lua_settop(NL, 0);

    log_trace("ispawn from L:%p, NL:%p", L, NL);
    luaL_loadbuffer(NL, code, str_len, "NL");

    luvco_state* state = luvco_init_state(NL);
    state->main_coro = NL;
    lua_newtable(NL);
    lua_pushinteger(NL, 1);
    lua_seti(NL, -2, coro_table_count_index);
    lua_setfield(NL, LUA_REGISTRYINDEX, coro_table_name);

    luvco_resume(NL, 0);
    return 0;
}

static const luaL_Reg base_lib [] = {
    { "spawn", spawn },
    { "ispawn", ispawn },
    { NULL, NULL }
};

int luvco_open_base (lua_State* L) {
    luaL_newlib(L, base_lib);
    return 1;
}

luvco_state* luvco_init (lua_State* L) {
    log_trace("luvco init in L:%p", L);
    luvco_state* state = luvco_init_state(L);
    state->main_coro = L;

    lua_newtable(L);
    lua_pushinteger(L, 1);
    lua_seti(L, -2, coro_table_count_index);
    lua_setfield(L, LUA_REGISTRYINDEX, coro_table_name);
    return state;
}

void luvco_run (luvco_state* state) {
    log_trace("luvco run in State:%p", state);
    luvco_resume(state->main_coro, 0);
    uv_run(&state->loop, UV_RUN_DEFAULT);
    log_trace("luvco end in State:%p", state);
}
