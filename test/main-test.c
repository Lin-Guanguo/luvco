#include <stdio.h>
#include <stdlib.h>

#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>

#include <luvco.h>
#include <luvco/tools.h>

int main(int argc, char **argv) {
    lua_State *L = luaL_newstate();

    lua_settop(L, 0);

    if (argc == 2) {
        luaL_loadfile(L, argv[1]);
    } else {
        luaL_loadfile(L, "../lua/test.lua");
    }

    luvco_state* state = luvco_init(L, NULL, NULL);
    luvco_run(state);
    luvco_close(state);

    printf("main end");
}