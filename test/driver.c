#include <luvco.h>
#include <luvco/log.h>

#include <lua/lua.h>
#include <lua/lauxlib.h>

#include <stdio.h>

int main(int argc, char **argv) {
    log_set_level(LOG_ERROR);

    if (argc != 2) {
        printf("usage: %s %s", argv[0], "file.lua");
        return 1;
    }
    lua_State *L = luaL_newstate();

    luaL_openlibs(L);
    luaL_requiref(L, "luvco", luvco_open_base, 1);
    luaL_requiref(L, "luvco_net", luvco_open_net, 1);

    lua_settop(L, 0);
    luaL_loadfile(L, argv[1]);

    luvco_gstate* state = luvco_init(L, NULL, NULL);
    luvco_run(state);
}