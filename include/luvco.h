#pragma once

typedef struct lua_State lua_State;
typedef struct luvco_gstate luvco_gstate;
typedef lua_State* (*luvco_newluaf) (void* ud);

luvco_gstate* luvco_init (lua_State* L, luvco_newluaf f, void* f_ud);

void luvco_run (luvco_gstate* L);

void luvco_close (luvco_gstate* state);

int luvco_open_base (lua_State* L);

// if has global table name "luvco", push lib as it's field name "net",
// use it like `luvco.net.new_server(addr)`
int luvco_open_net (lua_State* L);

