#pragma once

typedef struct lua_State lua_State;
typedef struct luvco_gstate luvco_gstate;
typedef lua_State* (*luvco_newluaf) (void* ud);

luvco_gstate* luvco_init (lua_State* L, luvco_newluaf f, void* f_ud);

void luvco_run (luvco_gstate* L);

void luvco_close (luvco_gstate* state);

