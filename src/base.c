#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>

#include <uv.h>
#include <luvco.h>
#include <luvco/tools.h>

static const int THIS_LIB_ASYNC_YIELD_ID = 0;

static int luvco_spawn_local_k (lua_State* L, int status, lua_KContext ctx) {
    return 0;
}

static int luvco_spawn_local (lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    push_async_yield_tag(L, 0);
    lua_yieldk(L, 0, (lua_KContext)NULL, luvco_spawn_local_k);
    // yield, let luvco context to luvco_spawn_local_c
}

static const luaL_Reg base_lib [] = {
    { "spawn_local", luvco_spawn_local },
    { NULL, NULL }
};

int luvco_open_base (lua_State* L) {
    luaL_newlib(L, base_lib);
    return 1;
}