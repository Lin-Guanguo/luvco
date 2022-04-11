#include <luvco/tools.h>
#include <luvco/object.h>

#include <assert.h>
#include <stdlib.h>
#include <stdatomic.h>

typedef struct chan1 {
    // watting lua state
    // last bit = 1 mean watting to send
    // last bit = 0 mean watting to recv
    atomic_intptr_t watting;
} chan1;

static int move_udata(lua_State *from, lua_State *to) {
    log_trace("move obj from L:%p to L:%p", from, to);
    luvco_objhead_moveable* data = lua_touserdata(from, -1);
    assert(data != NULL);
    lua_getmetatable(from, -1);
    lua_getfield(from, -1, LUVCO_UDATAMETA_MOVEABLE_FIELD);
    bool ismoveable = lua_toboolean(from, -1);
    if (!ismoveable) {
        log_error("attemp to move value without filed %s", LUVCO_UDATAMETA_MOVEABLE_FIELD);
        return -1;
    }
    lua_getfield(from, -2, "__name");
    const char* datatype = lua_tostring(from, -1);
    lua_getfield(from, -3, LUVCO_UDATAMETA_SIZEOF_FIELD);
    int datasize = lua_tointeger(from, -1);

    int ty = luaL_getmetatable(to, datatype);
    if (ty == LUA_TNIL) {
        log_warn("chan receiver hanven't import corresponding package");
        lua_pop(to, 1);
        lua_pushnil(to);
    } else {
        luvco_objhead_moveable* newdata = lua_newuserdatauv(to, datasize, 0);
        // TODO: Test
        lua_rotate(to, -2, 1);
        luvco_dump_lua_stack(to);
        //
        lua_setmetatable(to, -2);
        memcpy(newdata, data, datasize);
    }
    lua_pop(from, 4); // metatable, __moveable, __name, __sizeof
}

// pop top of `from` , move it to `to`, if `to` not import
// corresponding package, push nil to `to`
//
// return -1 mean some error happen
//
// notice: `from` and `to` lua_State should not in running
static int move_cross_lua(lua_State *from, lua_State *to) {
    // TODO: check data type
    // move primary type and udata which __name begin with luvco.
    int ty = lua_type(from, -1);
    switch (ty) {
    case LUA_TNIL:
        lua_pushnil(to);
        break;
    case LUA_TBOOLEAN:
        lua_pushboolean(to, lua_toboolean(from, -1));
        break;
    case LUA_TNUMBER:
        lua_pushnumber(to, lua_tonumber(from, -1));
        break;
    case LUA_TSTRING:
        size_t len;
        const char* s = lua_tolstring(from, -1, &len);
        lua_pushlstring(to, s, len);
        break;
    case LUA_TLIGHTUSERDATA:
        lua_pushudata(to, lua_touserdata(from, -1));
        break;
    case LUA_TUSERDATA:
        return move_udata(from, to);
        break;
    default:
        assert(0 && "move invalid type");
    }
}

// send top of L to chan
// return NULL mean need to watting other lua_State to recv
// return (lua_State*)1 mean some error happen
// return pointer mean can send to that lua_State
static lua_State* chan1_trysend (lua_State *L, chan1* ch) {
    assert(((intptr_t)L & 1 == 0) && "lua_State's last bit is not zero, can't use as flag");
    lua_State_flag* L1 = (lua_State*)((intptr_t)L | 1); // mean watting to send
    lua_State_flag* L2 = NULL;
    bool res = atomic_compare_exchange_strong(&ch->watting, &L2, (intptr_t)L1);
    if (res) {
        return NULL;
    }
    if ((intptr_t)L2 & 1 == 1) {
        log_warn("send multiple at the same time in same chan1");
        return (lua_State*)1;
    }
    atomic_store(&ch->watting, 0);
    lua_State* Lto = (lua_State*)L2;
    return Lto;
}

// return NULL mean need to watting other lua_State to recv
// return (lua_State*)1 mean some error happen
// return pointer mean can recv from that lua_State
static int chan1_recv (lua_State *L, chan1* ch) {
    assert(((intptr_t)L & 1 == 0) && "lua_State's last bit is not zero, can't use as flag");
    lua_State_flag* L1 = (lua_State*)((intptr_t)L | 0); // mean watting to recv
    lua_State_flag* L2 = NULL;
    bool res = atomic_compare_exchange_strong(&ch->watting, &L2, (intptr_t)L1);
    if (res) {
        return NULL;
    }
    if ((intptr_t)L2 & 1 == 0) {
        log_warn("recv multiple at the same time in same chan1");
        return (lua_State*)1;
    }
    atomic_store(&ch->watting, 0);
    lua_State* Lfrom = (lua_State*)((intptr_t)L2 & (~1));
    return Lfrom;
}

typedef struct chan1_sender {
    chan1* ch;
} chan1_sender;

typedef struct chan1_recver {
    chan1* ch;
} chan1_recver;

// TODO: stop running 2 lua_State when attemp to sendchan1

static int new_chan1 (lua_State* L) {

    return 2;
}

static int lua_chan1_send_k (lua_State *L, int status, lua_KContext ctx) {
    lua_pushboolean(L, true);
    return 1;
}

static int lua_chan1_send (lua_State* L) {
    chan1_sender* sender = luvco_check_udata(L, 1, chan1_sender);
    chan1* ch = sender->ch;
    lua_State* ret = chan1_trysend(L, ch);
    if (ret == NULL) {
        // resume when other lua_State recv
        luvco_yield(L, (lua_KContext)NULL, lua_chan1_send_k);
    } else if (ret == (lua_State*)1) {
        lua_pushboolean(L, false);
        return 1;
    } else {
        // TODO: luvco_yield_thread, to yield this lua_State until other thread
        // resume it.
    }
}

static int lua_chan1_recv (lua_State* L) {

}

static const luaL_Reg sender_m [] = {
    { "send", lua_chan1_send },
    { NULL, NULL}
};

static const luaL_Reg recver_m [] = {
    { "recv", lua_chan1_recv },
    { NULL, NULL}
};

static int open_chan1 (lua_State* L) {
    luvco_new_meta(L, chan1_sender);
    luaL_setfuncs(L, sender_m, 0);
    luvco_new_meta(L, chan1_recver);
    luaL_setfuncs(L, recver_m, 0);
}



