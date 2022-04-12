#include <luvco/tools.h>
#include <luvco/object.h>
#include <luvco/scheduler.h>

#include <assert.h>
#include <stdlib.h>
#include <stdatomic.h>

enum luvco_move_return {
    LUVCO_MOVE_OK,
    LUVCO_MOVE_NONMOVEABLE_UDATA,
    LUVCO_MOVE_UDATA_TO_UNIMPORT_STATE,
};

static enum luvco_move_return move_udata(lua_State *from, lua_State *to) {
    log_trace("move obj from L:%p to L:%p", from, to);
    luvco_objhead_moveable* data = lua_touserdata(from, -1);
    assert(data != NULL);
    lua_getmetatable(from, -1);
    lua_getfield(from, -1, LUVCO_UDATAMETA_MOVEABLE_FIELD);
    bool ismoveable = lua_toboolean(from, -1);
    if (!ismoveable) {
        log_warn("attemp to move value without filed %s", LUVCO_UDATAMETA_MOVEABLE_FIELD);
        lua_pushnil(to);
        lua_pop(from, 2);
        return LUVCO_MOVE_NONMOVEABLE_UDATA;
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
        lua_pop(from, 4);
        return LUVCO_MOVE_UDATA_TO_UNIMPORT_STATE;
    }
    luvco_objhead_moveable* newdata = lua_newuserdatauv(to, datasize, 0);
    lua_rotate(to, -2, 1);
    lua_setmetatable(to, -2);
    memcpy(newdata, data, datasize);
    // TODO: set data moved

    lua_pop(from, 4); // metatable, __moveable, __name, __sizeof
    return LUVCO_MOVE_OK;
}

// pop top of `from` , move it to `to`,
// if some error happen, push nil to `to`
static enum luvco_move_return move_cross_lua(lua_State *from, lua_State *to) {
    int ty = lua_type(from, -1);
    size_t len;
    const char* s;
    switch (ty) {
    case LUA_TNIL:
        lua_pushnil(to);
        return LUVCO_MOVE_OK;
    case LUA_TBOOLEAN:
        lua_pushboolean(to, lua_toboolean(from, -1));
        return LUVCO_MOVE_OK;
    case LUA_TNUMBER:
        lua_pushnumber(to, lua_tonumber(from, -1));
        return LUVCO_MOVE_OK;
    case LUA_TSTRING:
        s = lua_tolstring(from, -1, &len);
        lua_pushlstring(to, s, len);
        return LUVCO_MOVE_OK;
    case LUA_TLIGHTUSERDATA:
        lua_pushlightuserdata(to, lua_touserdata(from, -1));
        return LUVCO_MOVE_OK;
    case LUA_TUSERDATA:
        return move_udata(from, to);
    default:
        assert(0 && "move invalid type");
    }
}

enum chan1_watting_state {
    CHAN1_WAITING_EMPTY,
    CHAN1_WAITING_TO_SEND,
    CHAN1_WAITING_TO_RECV,
};

typedef struct chan1 {
    atomic_char ref_count;
    luvco_spinlock mu;
    char waiting_state;
    lua_State* Lto;
    lua_State* Lfrom;
    luvco_lstate* Lto_lstate;
    luvco_lstate* Lfrom_lstate;
} chan1;

enum chan1_try_return {
    CHAN1_TRY_YIELD,
    CHAN1_TRY_ERROR,
    CHAN1_TRY_START,
};

static enum chan1_try_return chan1_trysend (lua_State* L, luvco_lstate* lstate, chan1* ch) {
    luvco_spinlock_lock(&ch->mu);
    int ret = 0;
    switch (ch->waiting_state) {
    case CHAN1_WAITING_EMPTY:
        ch->waiting_state = CHAN1_WAITING_TO_SEND;
        ch->Lfrom = L;
        ch->Lfrom_lstate = lstate;
        ret = CHAN1_TRY_YIELD;
        break;
    case CHAN1_WAITING_TO_SEND:
        log_warn("send multiple at the same time in same chan1");
        ret = CHAN1_TRY_ERROR;
        break;
    case CHAN1_WAITING_TO_RECV:
        ch->Lfrom = L;
        ch->Lfrom_lstate = lstate;
        ret = CHAN1_TRY_START;
        break;
    default:
        assert(0 && "Invalid state");
    }
    luvco_spinlock_unlock(&ch->mu);
    return ret;
}

static enum chan1_try_return chan1_tryrecv (lua_State *L, luvco_lstate* lstate, chan1* ch) {
    luvco_spinlock_lock(&ch->mu);
    int ret = 0;
    switch (ch->waiting_state) {
    case CHAN1_WAITING_EMPTY:
        ch->waiting_state = CHAN1_WAITING_TO_RECV;
        ch->Lto = L;
        ch->Lto_lstate = lstate;
        ret = CHAN1_TRY_YIELD;
        break;
    case CHAN1_WAITING_TO_RECV:
        log_warn("recv multiple at the same time in same chan1");
        ret = CHAN1_TRY_ERROR;
        break;
    case CHAN1_WAITING_TO_SEND:
        ch->Lto = L;
        ch->Lto_lstate = lstate;
        ret = CHAN1_TRY_START;
        break;
    default:
        assert(0 && "Invalid state");
    }
    luvco_spinlock_unlock(&ch->mu);
    return ret;
}

typedef struct chan1_sender {
    chan1* ch;
} chan1_sender;

typedef struct chan1_recver {
    chan1* ch;
} chan1_recver;

static int lua_chan1_send_k (lua_State *L, int status, lua_KContext ctx) {
    if ((void*)ctx == NULL) {
        return 1;
    }
    chan1* ch = (chan1*)ctx;
    log_trace("chan1:%p step3, start in send_k of L:%p", ch, L);
    luvco_gstate* gstate = luvco_get_gstate(L);
    luvco_scheduler* scheduler = gstate->scheduler;
    assert(ch->Lfrom == L);
    int moveret = move_cross_lua(ch->Lfrom, ch->Lto);
    if (moveret == LUVCO_MOVE_OK) {
        lua_pushboolean(ch->Lfrom, true);
        lua_pushboolean(ch->Lto, true);
        lua_rotate(ch->Lto, -2, 1);
    } else {
        lua_pushboolean(ch->Lfrom, false);
        lua_pushboolean(ch->Lto, false);
        lua_rotate(ch->Lto, -2, 1);
    }
    luvco_scheduler_addwork(scheduler, ch->Lto_lstate);
    ch->waiting_state = CHAN1_WAITING_EMPTY;
    return 1;
}

static int lua_chan1_recv_k (lua_State *L, int status, lua_KContext ctx) {
    if ((void*)ctx == NULL) {
        return 2;
    }
    chan1* ch = (chan1*)ctx;
    log_trace("chan1:%p step3, start in recv_k of L:%p", ch, L);
    luvco_gstate* gstate = luvco_get_gstate(L);
    luvco_scheduler* scheduler = gstate->scheduler;
    assert(ch->Lto == L);
    int moveret = move_cross_lua(ch->Lfrom, ch->Lto);
    if (moveret == LUVCO_MOVE_OK) {
        lua_pushboolean(ch->Lfrom, true);
        lua_pushboolean(ch->Lto, true);
        lua_rotate(ch->Lto, -2, 1);
    } else {
        lua_pushboolean(ch->Lfrom, false);
        lua_pushboolean(ch->Lto, false);
        lua_rotate(ch->Lto, -2, 1);
    }
    luvco_scheduler_addwork(scheduler, ch->Lfrom_lstate);
    ch->waiting_state = CHAN1_WAITING_EMPTY;
    return 2;
}

static void lua_chan1_send_after_yield (void* ud) {
    chan1* ch = (chan1*)ud;
    luvco_toresume(ch->Lto_lstate, ch->Lto, 0);
    // toresume, then will run lua_chan1_recv_k
}

// return a boolean
static int lua_chan1_send (lua_State* L) {
    chan1_sender* sender = luvco_check_udata(L, 1, chan1_sender);
    luaL_checkany(L, 2);

    luvco_lstate* lstate = luvco_get_lstate(L);
    chan1* ch = sender->ch;
    int ret = chan1_trysend(L, lstate, ch);
    switch (ret) {
    case CHAN1_TRY_YIELD:
        log_trace("chan1:%p step1, send, yield in L:%p", ch, L);
        // resume by lua_chan1_recv_after_yield
        luvco_yield(L, (lua_KContext)ch, lua_chan1_send_k);
        break;
    case CHAN1_TRY_ERROR:
        lua_pushboolean(L, false);
        return 1;
    case CHAN1_TRY_START:
        log_trace("chan1:%p step2, send start, yield thread L:%p", ch, L);
        luvco_toresume(lstate, L, 0);
        luvco_yield_thread(L, (lua_KContext)NULL, lua_chan1_send_k, lua_chan1_send_after_yield, (void*)ch);
        // then thread will yield, start run by lua_chan1_recv_k
    default:
        assert(0 && "Unexpected chan1 return");
    }
    assert(0 && "Never reach branch");
}

static void lua_chan1_recv_after_yield (void* ud) {
    chan1* ch = (chan1*)ud;
    luvco_toresume(ch->Lfrom_lstate, ch->Lfrom, 0);
    // toresume, then will run lua_chan1_send_k
}

// return a boolean an a value
static int lua_chan1_recv (lua_State* L) {
    chan1_recver* recver = luvco_check_udata(L, 1, chan1_recver);
    luvco_lstate* lstate = luvco_get_lstate(L);
    chan1* ch = recver->ch;
    int ret = chan1_tryrecv(L, lstate, ch);
    switch (ret) {
    case CHAN1_TRY_YIELD:
        log_trace("chan1:%p step1, recv, yield in L:%p", ch, L);
        // resume by lua_chan1_send_after_yield
        luvco_yield(L, (lua_KContext)ch, lua_chan1_recv_k);
        break;
    case CHAN1_TRY_ERROR:
        lua_pushboolean(L, false);
        return 1;
    case CHAN1_TRY_START:
        log_trace("chan1:%p step2, recv start, yield thread L:%p", ch, L);
        luvco_toresume(lstate, L, 0);
        luvco_yield_thread(L, (lua_KContext)NULL, lua_chan1_recv_k, lua_chan1_recv_after_yield, (void*)ch);
        // then thread will yield, start run by lua_chan1_send_k
    default:
        assert(0 && "Unexpected chan1 return");
    }
    assert(0 && "Never reach brach");
}

static int lua_chan1_sender_gc (lua_State* l) {
    chan1_sender* sender = luvco_check_udata(l, 1, chan1_sender);
    int oldcount = atomic_fetch_sub(&sender->ch->ref_count, 1);
    if (oldcount == 1) {
        free(sender->ch);
    }
    return 0;
    // TODO: if recv are watting, return nil
}

static int lua_chan1_recver_gc (lua_State* l) {
    chan1_recver* recver = luvco_check_udata(l, 1, chan1_recver);
    int oldcount = atomic_fetch_sub(&recver->ch->ref_count, 1);
    if (oldcount == 1) {
        free(recver->ch);
    }
    return 0;
}

static const luaL_Reg sender_m [] = {
    { "send", lua_chan1_send },
    { NULL, NULL}
};

static const luaL_Reg recver_m [] = {
    { "recv", lua_chan1_recv },
    { NULL, NULL}
};

void luvco_chan1_cross_state (lua_State *Lsend, lua_State *Lrecv) {
    chan1* ch = (chan1*)malloc(sizeof(chan1));
    atomic_store(&ch->ref_count, 2);
    luvco_spinlock_init(&ch->mu);
    ch->waiting_state = CHAN1_WAITING_EMPTY;
    chan1_sender* sender = luvco_pushudata_with_meta(Lsend, chan1_sender);
    chan1_recver* recver = luvco_pushudata_with_meta(Lrecv, chan1_recver);
    sender->ch = ch;
    recver->ch = ch;
}

int luvco_open_chan (lua_State* L) {
    luvco_new_meta(L, chan1_sender);
    luaL_setfuncs(L, sender_m, 0);
    luvco_new_meta(L, chan1_recver);
    luaL_setfuncs(L, recver_m, 0);
    luvco_open_chan_withbase(L);

    lua_newtable(L);
    return 1;
}

