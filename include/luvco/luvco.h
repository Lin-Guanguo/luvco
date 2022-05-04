#pragma once
#include <luvco.h>
#include <luvco/log.h>

#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <uv.h>

#include <stdatomic.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>


// for debug
void luvco_dump_lua_stack (lua_State *L);

typedef void (*luvco_cb_f) (void*);
typedef struct luvco_ringbuf2 luvco_ringbuf2;
typedef struct luvco_scheduler luvco_scheduler;

typedef struct luvco_gstate {
    uv_loop_t loop;
    lua_State* main_coro;
    luvco_newluaf newluaf;
    void* newluaf_ud;
    luvco_scheduler* scheduler;
    luvco_ringbuf2* uvworklist; // element is luvco_uvwork*, only pop by eventloop thread
} luvco_gstate;

typedef struct luvco_lstate {
    luvco_gstate *gstate;
    luvco_ringbuf2* toresume;
    size_t coro_count;
    bool is_main_coro;
    int last_yield_tag;
    void* last_yield_args[4];
} luvco_lstate;

luvco_lstate* luvco_get_state (lua_State* L);
void luvco_yield (lua_State *L, lua_KContext k_ctx, lua_KFunction k);
void luvco_yield_thread (lua_State *L, lua_KContext k_ctx, lua_KFunction k, luvco_cb_f run_after_yield, void* f_ud);
void luvco_toresume (luvco_lstate* lstate, lua_State *L, int nargs);

enum luvco_resume_return {
    LUVCO_RESUME_NORMAL = 0,
    LUVCO_RESUME_LSTATE_END,
    LUVCO_RESUME_YIELD_THREAD,
    LUVCO_RESUME_ERROR,
};
typedef lua_State lua_State_flag;
// call by scheduler
enum luvco_resume_return luvco_resume (lua_State_flag* L2);

int luvco_open_base (lua_State* L);
int luvco_open_net (lua_State* L);
int luvco_open_chan (lua_State* L);
void luvco_open_chan_withbase (lua_State* L);



typedef atomic_flag luvco_spinlock;
#define luvco_spinlock_init(spin) atomic_flag_clear(spin)
#define luvco_spinlock_lock(spin) while (atomic_flag_test_and_set(spin)) {}
#define luvco_spinlock_unlock(spin) atomic_flag_clear(spin)



enum luvco_move_return {
    LUVCO_MOVE_OK,
    LUVCO_MOVE_NONMOVEABLE_UDATA,
    LUVCO_MOVE_UDATA_TO_UNIMPORT_STATE,
    LUVCO_MOVE_FAILED,
};
enum luvco_move_return luvco_move_cross_lua(lua_State *from, lua_State *to);
void luvco_chan1_build (lua_State *Lsend, lua_State *Lrecv);


#define container_of(ptr, type, member) (type*)((char*)(ptr) - (char*)(&(((type*)NULL)->member)))
#define luvco_new_meta(L, type) \
    luaL_newmetatable((L), "luvco."#type); \
    lua_pushvalue((L), -1); \
    lua_setfield((L), -2, "__index")
const char* LUVCO_UDATAMETA_SIZEOF_FIELD;
const char* LUVCO_UDATAMETA_MOVEF_FIELD;
#define luvco_new_meta_moveable(L, type, move_f) \
    luaL_newmetatable((L), "luvco."#type); \
    lua_pushvalue((L), -1); \
    lua_setfield((L), -2, "__index"); \
    lua_pushinteger((L), sizeof(type)); \
    lua_setfield(L, -2, LUVCO_UDATAMETA_SIZEOF_FIELD); \
    lua_pushlightuserdata((L), move_f); \
    lua_setfield(L, -2, LUVCO_UDATAMETA_MOVEF_FIELD)
#define luvco_pushudata_with_meta(L, type) \
    (type*)lua_newuserdatauv((L), sizeof(type), 0); \
    luaL_setmetatable((L), "luvco."#type)
#define luvco_check_udata(L, n, type) \
    (type*)luaL_checkudata((L), (n), "luvco."#type)

#define luvco_cbdata \
    luvco_uvwork uv; lua_State* L; luvco_lstate* lstate
#define luvco_cbdata_set(obj, L, lstate) \
    (obj).L=(L); (obj).lstate=lstate
#define luvco_cbdata_extract(obj) \
    lua_State* L = (obj).L; \
    luvco_lstate* lstate = (obj).lstate; \
    luvco_gstate* gstate = lstate->gstate
#define luvco_toresume_incb(obj, nargs) do {\
    lua_State* L = (obj).L; \
    (obj).L = NULL; \
    luvco_toresume((obj).lstate, L, (nargs));} while(0)
// return 0 mean move successfully
typedef int (*luvco_moveobj_f) (void* from, void* to);



typedef struct luvco_ringbuf luvco_ringbuf;
typedef struct luvco_ringbuf2 luvco_ringbuf2;

size_t luvco_ringbuf_sizeof (int len);
void luvco_ringbuf_init (luvco_ringbuf* r, int len);
int luvco_ringbuf_push (luvco_ringbuf* r, void* data);
#define luvco_ringbuf_spinpush(r, data) while (luvco_ringbuf_push((r), (data)) != 0);
int luvco_ringbuf_unlockpush (luvco_ringbuf* r, void* data);
int luvco_ringbuf_pop (luvco_ringbuf* r, void** data);
int luvco_ringbuf_unlockpop (luvco_ringbuf* r, void** data);

size_t luvco_ringbuf2_sizeof (int len);
void luvco_ringbuf2_init (luvco_ringbuf2* r, int len, int firstbufsize);
int luvco_ringbuf2_push (luvco_ringbuf2* r, void* data);
#define luvco_ringbuf2_spinpush(r, data) while (luvco_ringbuf2_push((r), (data)) != 0);
int luvco_ringbuf2_unlockpush (luvco_ringbuf2* r, void* data);
int luvco_ringbuf2_pop (luvco_ringbuf2* r, void** data);
int luvco_ringbuf2_unlockpop (luvco_ringbuf2* r, void** data);
int luvco_ringbuf2_delete (luvco_ringbuf2* r);



typedef struct luvco_scheduler luvco_scheduler;
size_t luvco_scheduler_sizeof (int nprocess);
void luvco_scheduler_init (luvco_scheduler* s, int nprocess);
void luvco_scheduler_stop (luvco_scheduler* s);
void luvco_scheduler_delete (luvco_scheduler* s);
int luvco_scheduler_addwork (luvco_scheduler* s, luvco_lstate* l);
// resume a work after luvco_yield_thread
int luvco_scheduler_resumework (luvco_scheduler* s, luvco_lstate* l);
int luvco_scheduler_totalwork (luvco_scheduler* s);

typedef struct luvco_uvwork luvco_uvwork;
typedef void (*luvco_uvwork_cb) (luvco_uvwork* work);
typedef struct luvco_uvwork {
    luvco_uvwork_cb cb;
} luvco_uvwork;
void luvco_add_uvwork(luvco_gstate* gstate, luvco_uvwork* uvwork);
