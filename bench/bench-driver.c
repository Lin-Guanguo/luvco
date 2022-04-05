#include <stdio.h>
#include <stdlib.h>

#include <lua/lua.h>
#include <lua/lualib.h>
#include <lua/lauxlib.h>

#include <luvco.h>
#include <luvco/log.h>

typedef struct lua_memory_data {
    size_t cur_memroy;
    size_t max_memroy;
    size_t alloc_times;
} lua_memory_data;

static void* my_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)osize;  /* not used */
    lua_memory_data* data = (lua_memory_data*)ud;
    if (nsize == 0) {
        data->cur_memroy -= osize;
        free(ptr);
    } else {
        data->cur_memroy += nsize - osize;
        ptr = realloc(ptr, nsize);
    }

    data->alloc_times++;
    if (data->cur_memroy > data->max_memroy) {
        data->max_memroy = data->cur_memroy;
    }

    if (nsize == 0) {
        return NULL;
    } else {
        return ptr;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: %s %s", argv[0], "file.lua");
        return 1;
    }

    lua_memory_data data;
    data.cur_memroy = 0;
    data.max_memroy = 0;
    data.alloc_times = 0;
    lua_State *L = lua_newstate(my_alloc, &data);

    luaL_openlibs(L);
    luaL_requiref(L, "luvco", luvco_open_base, 1);
    luaL_requiref(L, "luvco_net", luvco_open_net, 1);

    lua_settop(L, 0);
    luaL_loadfile(L, argv[1]);

    luvco_gstate* state = luvco_init(L, NULL, NULL);
    luvco_run(state);

    log_info("max memory %ul", data.max_memroy);
}