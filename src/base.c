#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>

#include <uv.h>
#include <luvco.h>
#include <luvco/tools.h>

static void yield_spawn_local (lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_State* NL = luvco_new_co(L);

    lua_pushvalue(L, 1); // copy function to top
    lua_xmove(L, NL, 1);  // pop function from L to NL

    printf("spawn local: %p\n", NL);

    int res;
    luvco_resume(L, 0, &res);
    luvco_resume(NL, 0, &res);
}

static int spawn_local_k (lua_State* L, int status, lua_KContext ctx) {
    return 0;
}

static int spawn_local (lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    push_async_yield_tag(L, &yield_spawn_local);
    lua_yieldk(L, 0, (lua_KContext)NULL, spawn_local_k);
}

static const luaL_Reg base_lib [] = {
    { "spawn_local", spawn_local },
    { NULL, NULL }
};

int luvco_open_base (lua_State* L) {
    luaL_newlib(L, base_lib);
    return 1;
}