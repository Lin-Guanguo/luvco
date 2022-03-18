#pragma once
#include <lua/lua.h>
#include <uv.h>

typedef struct luvco_state {
    uv_loop_t loop;
} luvco_state;

int luvco_run (lua_State* L);

luvco_state* luvco_get_state (lua_State* L);

int luvco_open_base (lua_State* L);

// if has global table name "luvco", push lib as it's field name "net",
// use it like `luvco.net.new_server(addr)`
int luvco_open_net (lua_State* L);

