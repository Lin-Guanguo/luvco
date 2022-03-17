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

static void unregister_coro (lua_State* L) {
    printf ("unregister_coro %p\n", L);
    lua_pushlightuserdata(L, (void*)L);
    lua_pushnil(L);
    lua_settable(L, LUA_REGISTRYINDEX);
}

lua_State* luvco_new_co(lua_State* L) {
    lua_State* NL = lua_newthread(L);
    register_coro(L);
    return NL;
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

void luvco_yield (lua_State* L) {
    void* yield_tag = lua_touserdata(L, -1);
    if (yield_tag != (void*)&luvco_yield) {
        printf("State:%p return\n", L);
        if (lua_gettop(L) != 0) {
            printf("    stack no empty ");
            luvco_dump_lua_stack(L);
        }
        unregister_coro(L);
        return;
    }

    luvco_yield_cb cb = (luvco_yield_cb)lua_touserdata(L, -2);
    luvco_dump_lua_stack(L);
    lua_pop(L, 2);

    if (cb != NULL) {
        cb(L); // else cb is register by yield fun
    }
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


