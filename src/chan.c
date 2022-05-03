#include <luvco/chan.h>

#include <luvco/object.h>
#include <luvco/lock.h>
#include <luvco/scheduler.h>

#include <lua/lua.h>
#include <lua/lauxlib.h>

#include <assert.h>
#include <stdlib.h>

// call movef, move top of `from` to `to`, not pop
static enum luvco_move_return move_udata(lua_State *from, lua_State *to) {
    log_trace("move obj from L:%p to L:%p", from, to);
    void* data = lua_touserdata(from, -1);
    assert(data != NULL);
    int ret = lua_getmetatable(from, -1);
    if (ret == 0) { // no metatable
        return LUVCO_MOVE_NONMOVEABLE_UDATA;
    }
    int ty = lua_getfield(from, -1, LUVCO_UDATAMETA_MOVEF_FIELD);
    luvco_moveobj_f movef = (luvco_moveobj_f)lua_touserdata(from, -1);
    if (ty != LUA_TLIGHTUSERDATA || movef == NULL) {
        log_warn("attemp to move value without filed %s", LUVCO_UDATAMETA_MOVEF_FIELD);
        lua_pushnil(to);
        lua_pop(from, 2); // metatable, __movef
        return LUVCO_MOVE_NONMOVEABLE_UDATA;
    }

    ty = lua_getfield(from, -2, "__name");
    assert(ty == LUA_TSTRING);
    const char* datatype = lua_tostring(from, -1);

    ty = lua_getfield(from, -3, LUVCO_UDATAMETA_SIZEOF_FIELD);
    assert(ty == LUA_TNUMBER);
    int datasize = lua_tointeger(from, -1);

    ty = luaL_getmetatable(to, datatype);
    if (ty == LUA_TNIL) {
        log_warn("chan receiver hanven't import corresponding package");
        lua_pop(to, 1); // pop metatable
        lua_pushnil(to);
        lua_pop(from, 4);
        return LUVCO_MOVE_UDATA_TO_UNIMPORT_STATE;
    }
    void* newdata = lua_newuserdatauv(to, datasize, 0);
    lua_rotate(to, -2, 1);
    lua_setmetatable(to, -2);
    lua_pop(from, 4); // metatable, __movef, __name, __sizeof

    ret = movef(data, newdata);
    if (ret != 0) {
        lua_pop(to, 1); // pop move failed udata
        lua_pushnil(to);
        return LUVCO_MOVE_FAILED;
    }
    return LUVCO_MOVE_OK;
}

// move top of 'from' to `to`,
// if some error happen, push nil to `to`
//
// not pop `from`
enum luvco_move_return luvco_move_cross_lua(lua_State *from, lua_State *to) {
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

enum chan1_waiting_state {
    CHAN1_WAITING_EMPTY,
    CHAN1_WAITING_TO_SEND,
    CHAN1_WAITING_TO_RECV,

    CHAN1_WAITING_SENDER_CLOSE,
    CHAN1_WAITING_RECVER_CLOSE,
};

typedef struct chan1 {
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
    CHAN1_TRY_OTHER_CLOSE,
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
    case CHAN1_WAITING_RECVER_CLOSE:
        ret = CHAN1_TRY_OTHER_CLOSE;
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
    case CHAN1_WAITING_SENDER_CLOSE:
        ret = CHAN1_TRY_OTHER_CLOSE;
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

static int luvco_chan1_send_k (lua_State *L, int status, lua_KContext ctx) {
    if ((void*)ctx == NULL) {
        return 1;
    }
    chan1* ch = (chan1*)ctx;
    luvco_lstate* lstate = luvco_get_state(L);
    luvco_gstate* gstate = lstate->gstate;
    luvco_scheduler* scheduler = gstate->scheduler;

    if (ch->waiting_state == CHAN1_WAITING_RECVER_CLOSE) {
        // recver gc, send failed
        log_warn("chan1:%p, step3 failed, recver gc", ch);
        lua_pushboolean(ch->Lfrom, false);
        return 1;
    }

    log_trace("chan1:%p step3, start in send_k of L:%p", ch, L);
    assert(ch->Lfrom == L);
    int moveret = luvco_move_cross_lua(ch->Lfrom, ch->Lto);
    if (moveret == LUVCO_MOVE_OK) {
        lua_pushboolean(ch->Lfrom, true);
        lua_pushboolean(ch->Lto, true);
        lua_rotate(ch->Lto, -2, 1);
    } else {
        log_warn("move obj error, code=%d", moveret);
        lua_pushboolean(ch->Lfrom, false);
        lua_pushboolean(ch->Lto, false);
        lua_rotate(ch->Lto, -2, 1);
    }
    ch->waiting_state = CHAN1_WAITING_EMPTY;
    luvco_scheduler_resumework(scheduler, ch->Lto_lstate);
    return 1;
}

static int luvco_chan1_recv_k (lua_State *L, int status, lua_KContext ctx) {
    if ((void*)ctx == NULL) {
        return 2;
    }
    chan1* ch = (chan1*)ctx;
    luvco_lstate* lstate = luvco_get_state(L);
    luvco_gstate* gstate = lstate->gstate;
    luvco_scheduler* scheduler = gstate->scheduler;

    if (ch->waiting_state == CHAN1_WAITING_SENDER_CLOSE) {
        // sender gc, recv failed
        log_warn("chan1:%p, step3 failed, sender gc", ch);
        lua_pushboolean(ch->Lto, false);
        lua_pushnil(ch->Lto);
        return 2;
    }

    log_trace("chan1:%p step3, start in recv_k of L:%p", ch, L);
    assert(ch->Lto == L);
    int moveret = luvco_move_cross_lua(ch->Lfrom, ch->Lto);
    if (moveret == LUVCO_MOVE_OK) {
        lua_pushboolean(ch->Lfrom, true);
        lua_pushboolean(ch->Lto, true);
        lua_rotate(ch->Lto, -2, 1);
    } else {
        log_warn("move obj error, code=%d", moveret);
        lua_pushboolean(ch->Lfrom, false);
        lua_pushboolean(ch->Lto, false);
        lua_rotate(ch->Lto, -2, 1);
    }
    ch->waiting_state = CHAN1_WAITING_EMPTY;
    luvco_scheduler_resumework(scheduler, ch->Lfrom_lstate);
    return 2;
}

static void luvco_chan1_send_after_yield (void* ud) {
    chan1* ch = (chan1*)ud;
    luvco_toresume(ch->Lto_lstate, ch->Lto, 0);
    // toresume, then will run luvco_chan1_recv_k
}

// return a boolean
int luvco_chan1_send (lua_State* L) {
    chan1_sender* sender = luvco_check_udata(L, 1, chan1_sender);
    luaL_checkany(L, 2);

    luvco_lstate* lstate = luvco_get_state(L);
    chan1* ch = sender->ch;
    int ret = chan1_trysend(L, lstate, ch);
    switch (ret) {
    case CHAN1_TRY_YIELD:
        log_trace("chan1:%p step1, send, yield in L:%p", ch, L);
        // resume by luvco_chan1_recv_after_yield
        luvco_yield(L, (lua_KContext)ch, luvco_chan1_send_k);
        break;
    case CHAN1_TRY_ERROR:
    case CHAN1_TRY_OTHER_CLOSE:
        lua_pushboolean(L, false);
        return 1;
    case CHAN1_TRY_START:
        log_trace("chan1:%p step2, send start, yield thread L:%p", ch, L);
        luvco_toresume(lstate, L, 0);
        luvco_yield_thread(L, (lua_KContext)NULL, luvco_chan1_send_k, luvco_chan1_send_after_yield, (void*)ch);
        // then thread will yield, start run by luvco_chan1_recv_k
    default:
        assert(0 && "Unexpected chan1 return");
    }
    assert(0 && "Never reach branch");
}

static void luvco_chan1_recv_after_yield (void* ud) {
    chan1* ch = (chan1*)ud;
    luvco_toresume(ch->Lfrom_lstate, ch->Lfrom, 0);
    // toresume, then will run luvco_chan1_send_k
}

// return a boolean an a value
int luvco_chan1_recv (lua_State* L) {
    chan1_recver* recver = luvco_check_udata(L, 1, chan1_recver);
    luvco_lstate* lstate = luvco_get_state(L);
    chan1* ch = recver->ch;
    int ret = chan1_tryrecv(L, lstate, ch);
    switch (ret) {
    case CHAN1_TRY_YIELD:
        log_trace("chan1:%p step1, recv, yield in L:%p", ch, L);
        // resume by luvco_chan1_send_after_yield
        luvco_yield(L, (lua_KContext)ch, luvco_chan1_recv_k);
        break;
    case CHAN1_TRY_ERROR:
    case CHAN1_TRY_OTHER_CLOSE:
        lua_pushboolean(L, false);
        lua_pushnil(L);
        return 2;
    case CHAN1_TRY_START:
        log_trace("chan1:%p step2, recv start, yield thread L:%p", ch, L);
        luvco_toresume(lstate, L, 0);
        luvco_yield_thread(L, (lua_KContext)NULL, luvco_chan1_recv_k, luvco_chan1_recv_after_yield, (void*)ch);
        // then thread will yield, start run by luvco_chan1_send_k
    default:
        assert(0 && "Unexpected chan1 return");
    }
    assert(0 && "Never reach brach");
}

static int luvco_chan1_sender_gc (lua_State* l) {
    chan1_sender* sender = luvco_check_udata(l, 1, chan1_sender);
    chan1* ch = sender->ch;
    luvco_spinlock_lock(&ch->mu);
    log_debug("chan1:%p sender gc", ch);

    if (ch->waiting_state == CHAN1_WAITING_RECVER_CLOSE) {
        free(ch);
    } else if (ch->waiting_state == CHAN1_WAITING_TO_RECV) {
        // if recver close, sender still waiting to send
        ch->waiting_state = CHAN1_WAITING_SENDER_CLOSE;
        luvco_toresume(ch->Lto_lstate, ch->Lto, 0);
    }
    luvco_spinlock_unlock(&ch->mu);
    return 0;
}

static int luvco_chan1_recver_gc (lua_State* l) {
    chan1_recver* recver = luvco_check_udata(l, 1, chan1_recver);
    chan1* ch = recver->ch;
    luvco_spinlock_lock(&ch->mu);
    log_debug("chan1:%p recver gc", ch);

    if (ch->waiting_state == CHAN1_WAITING_SENDER_CLOSE) {
        free(ch);
    } else if (ch->waiting_state == CHAN1_WAITING_TO_SEND) {
        // if recver close, sender still waiting to send
        ch->waiting_state = CHAN1_WAITING_RECVER_CLOSE;
        luvco_toresume(ch->Lfrom_lstate, ch->Lfrom, 0);
    }
    luvco_spinlock_unlock(&ch->mu);
    return 0;
}

static const luaL_Reg sender_m [] = {
    { "send", luvco_chan1_send },
    { "__gc", luvco_chan1_sender_gc },
    { NULL, NULL}
};

static const luaL_Reg recver_m [] = {
    { "recv", luvco_chan1_recv },
    { "__gc", luvco_chan1_recver_gc },
    { NULL, NULL}
};

void luvco_chan1_build (lua_State *Lsend, lua_State *Lrecv) {
    chan1* ch = (chan1*)malloc(sizeof(chan1));
    luvco_spinlock_init(&ch->mu);
    ch->waiting_state = CHAN1_WAITING_EMPTY;
    chan1_sender* sender = luvco_pushudata_with_meta(Lsend, chan1_sender);
    chan1_recver* recver = luvco_pushudata_with_meta(Lrecv, chan1_recver);
    sender->ch = ch;
    recver->ch = ch;
    log_debug("build chan1:%p. Lfrom:%p, Lto:%p", ch, Lsend, Lrecv);
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

