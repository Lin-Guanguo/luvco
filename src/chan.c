#include <luvco/tools.h>
#include <luvco/object.h>

#include <assert.h>
#include <stdlib.h>
#include <stdatomic.h>

typedef struct luvco_chan1 {
    // watting lua state
    // last bit = 1 mean watting to send
    // last bit = 0 mean watting to recv
    atomic_intptr_t watting;
} luvco_chan1;

// pop top of `from` , move it to `to`, if `to` not import
// corresponding package, push nil to `to`
static void move_cross_lua(lua_State *from, lua_State *to) {
    log_trace("move obj from L:%p to L:%p", from, to);
    luvco_objhead* data = lua_touserdata(from, -1);
    lua_getmetatable(from, -1);
    lua_getfield(from, -1, "__name");
    const char* datatype = lua_tostring(from, -1);
    lua_getfield(from, -2, LUVCO_UDATAMETA_SIZEOF_FIELD);
    int datasize = lua_tointeger(from, -1);

    luaL_getmetatable(to, datatype);
    int ty = lua_type(to, -1);
    if (ty == LUA_TNIL) {
        log_warn("chan receiver hanven't import corresponding package");
        lua_pop(to, 1);
        lua_pushnil(to);
    } else {
        luvco_objhead* newdata = lua_newuserdatauv(to, datasize, 0);
        // TODO: Test
        lua_rotate(to, -2, 1);
        luvco_dump_lua_stack(to);
        //
        lua_setmetatable(to, -2);
        memcpy(newdata, data, datasize);
    }
    lua_pop(from, 4); // udata, metatable, __name, __sizeof
}

// return 0 mean ok, -1 mean error, 1 mean watting
static int chan1_send (lua_State *L) {
    assert(((intptr_t)L & 1 == 0) && "lua_State's last bit is not zero, can't use as flag");
    luvco_chan1* ch = luvco_check_udata(L, 1, luvco_chan1);
    lua_State_flag* L1 = (lua_State*)((intptr_t)L | 1); // mean watting to send
    lua_State_flag* L2 = NULL;
    bool res = atomic_compare_exchange_strong(&ch->watting, &L2, (intptr_t)L1);
    if (res) {
        return 1;
    }
    if ((intptr_t)L2 & 1 == 1) {
        log_warn("send multiple at the same time in same chan1");
        return -1;
    }
    atomic_store(&ch->watting, 0);
    lua_State* Lto = (lua_State*)L2;
    move_cross_lua(L, Lto);
    return 0;
}

// return 0 mean ok, -1 mean error, 1 mean watting
static int chan1_recv (lua_State *L) {
    assert(((intptr_t)L & 1 == 0) && "lua_State's last bit is not zero, can't use as flag");
    luvco_chan1* ch = luvco_check_udata(L, 1, luvco_chan1);
    lua_State_flag* L1 = (lua_State*)((intptr_t)L | 0); // mean watting to recv
    lua_State_flag* L2 = NULL;
    bool res = atomic_compare_exchange_strong(&ch->watting, &L2, (intptr_t)L1);
    if (res) {
        return 1;
    }
    if ((intptr_t)L2 & 1 == 0) {
        log_warn("recv multiple at the same time in same chan1");
        return -1;
    }
    atomic_store(&ch->watting, 0);
    lua_State* Lfrom = (lua_State*)((intptr_t)L2 & (~1));
    move_cross_lua(Lfrom, L);
    return 0;
}

typedef struct chan1_sender {

} chan1_sender;

typedef struct chan1_recver {

} chan1_recver;





