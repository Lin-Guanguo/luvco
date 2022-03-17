#pragma once
#include <lua/lua.h>
#include <uv.h>

typedef struct luvco_state {
    uv_loop_t loop;
} luvco_state;

int luvco_run (lua_State* L);

luvco_state* luvco_get_state (lua_State* L);

int luvco_open_base (lua_State* L);

int luvco_open_net (lua_State* L);

