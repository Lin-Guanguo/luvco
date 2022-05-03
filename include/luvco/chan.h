#pragma once

typedef struct lua_State lua_State;

enum luvco_move_return {
    LUVCO_MOVE_OK,
    LUVCO_MOVE_NONMOVEABLE_UDATA,
    LUVCO_MOVE_UDATA_TO_UNIMPORT_STATE,
    LUVCO_MOVE_FAILED,
};

enum luvco_move_return luvco_move_cross_lua(lua_State *from, lua_State *to);

int luvco_chan1_send (lua_State* L);

int luvco_chan1_recv (lua_State* L);

void luvco_chan1_build (lua_State *Lsend, lua_State *Lrecv);

void luvco_open_chan_withbase (lua_State* L);

int luvco_open_chan (lua_State* L);