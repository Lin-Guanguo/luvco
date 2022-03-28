#pragma once
#include <lua/lua.h>
#include <uv.h>

typedef struct luvco_state luvco_state;

luvco_state* luvco_init (lua_State* L);

void luvco_run (luvco_state* L);

int luvco_open_base (lua_State* L);

// if has global table name "luvco", push lib as it's field name "net",
// use it like `luvco.net.new_server(addr)`
int luvco_open_net (lua_State* L);

