#include <stdio.h>

#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>

#include <luvco.h>
#include <luvco/tools.h>

static int test_lua_c_fn (lua_State *L) {
    return 0;
}

int main() {
    lua_State *L = luaL_newstate();

    luaL_openlibs(L);
    luaL_requiref(L, "luvco", luvco_open_base, 1);
    lua_pop(L, 1);
    luaL_dofile(L, "../lua/test.lua");
    luvco_run(L);
}