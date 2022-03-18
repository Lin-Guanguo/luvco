#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>

#include <uv.h>
#include <luvco.h>
#include <luvco/tools.h>

// top of stack is coroutine thread.
// prevent gc collect coroutine
static void register_coro (lua_State* L) {
    luaL_checktype(L, -1, LUA_TTHREAD);
    printf ("register_coro   %p\n", lua_tothread(L, -1));
    lua_pushlightuserdata(L, (void*)L);
    lua_pushvalue(L, -2);
    lua_settable(L, LUA_REGISTRYINDEX);
}

static int unregister_coro_k (lua_State *L, int status, lua_KContext ctx) {
    assert(false && "should not resume");
    return 0;
}

static int unregister_coro (lua_State* L) {
    printf ("unregister_coro %p\n", L);
    lua_pushlightuserdata(L, (void*)L);
    lua_pushnil(L);
    lua_settable(L, LUA_REGISTRYINDEX);
    lua_yieldk(L, 0, (lua_KContext)NULL, unregister_coro_k);
    return 0;
}

// lua_state has only one shared luvco_state
static luvco_state* luvco_init_state (lua_State* L) {
    luvco_state* state = luvco_pushudata_with_meta(L, luvco_state);

    printf("luvco_init_state %p\n", state);
    uv_loop_init(&state->loop);
    lua_setfield(L, LUA_REGISTRYINDEX, "luvco.global_state");
    return state;
}

luvco_state* luvco_get_state (lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "luvco.global_state");
    luvco_state* state = (luvco_state*)lua_touserdata(L, -1);
    printf("get_luvco_state %p\n", state);

    lua_pop(L, 1);
    return state;
}

static int spawn_local_k (lua_State* L, int status, lua_KContext ctx) {
    return 0;
}

static int spawn_local (lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_State* NL = lua_newthread(L);
    register_coro(L);

    lua_pushvalue(L, 1); // copy function to top
    lua_xmove(L, NL, 1);  // pop function from L to NL

    printf("spawn local: %p\n", NL);

    int res;
    luvco_resume(NL, 0, &res);

    return 0;
}

// TODO: auto call free_co
static const luaL_Reg base_lib [] = {
    { "spawn_local", spawn_local },
    { "_free_co", unregister_coro },
    { NULL, NULL }
};

int luvco_open_base (lua_State* L) {
    luaL_newlib(L, base_lib);
    return 1;
}

int luvco_run (lua_State* L) {
    printf("luvco run start\n");

    luvco_new_meta(L, luvco_state);
    lua_pop(L, 1);

    luvco_state* state = luvco_init_state(L);

    int res;
    luvco_resume(L, 0, &res);

    uv_run(&state->loop, UV_RUN_DEFAULT);
    return 0;
}


