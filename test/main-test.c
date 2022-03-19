#include <stdio.h>
#include <stdlib.h>

#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>

#include <luvco.h>
#include <luvco/tools.h>

static int test_lua_c_fn (lua_State *L) {
    return 0;
}

size_t total_alloc = 0;

void* my_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;  /* not used */
    if (nsize == 0) {
        total_alloc -= osize;
        // printf("== free  %ld\n", osize);
        free(ptr);
        return NULL;
    } else {
        total_alloc += nsize - osize;
        // printf("== alloc %ld\t total = %ld\n", nsize - osize, total_alloc);
        return realloc(ptr, nsize);
    }
}

int main() {
    lua_State *L = lua_newstate(my_alloc, NULL);

    luaL_openlibs(L);
    luaL_requiref(L, "luvco", luvco_open_base, 1);
    luaL_requiref(L, "luvco_net", luvco_open_net, 1);

    lua_pop(L, 1);
    luaL_loadfile(L, "../lua/test.lua");

    luvco_run(L);
}