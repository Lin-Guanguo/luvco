#include <luvco.h>
#include <luvco/log.h>

#include <lua/lua.h>
#include <lua/lauxlib.h>

#include <stdio.h>

int main(int argc, char **argv) {
    log_set_level(LOG_ERROR);
    lua_State *L = luaL_newstate();
    if (argc == 2) {
        luaL_loadfile(L, argv[1]);
    } else {
        return -1;
    }

    luvco_gstate* state = luvco_init(L, NULL, NULL);
    luvco_run(state);
    luvco_close(state);
    lua_close(L);
}