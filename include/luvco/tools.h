#pragma once
#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <luvco/log.h>
#include <luvco.h>

#define luvco_new_meta(L, type)          \
    luaL_newmetatable((L), "luvco."#type);    \
    lua_pushvalue((L), -1);                   \
    lua_setfield((L), -2, "__index")

#define luvco_pushudata_with_meta(L, type) \
    (type*)lua_newuserdatauv((L), sizeof(type), 0); \
    luaL_setmetatable((L), "luvco."#type)

#define luvco_check_udata(L, n, type) \
    (type*)luaL_checkudata((L), (n), "luvco."#type)

#define container_of(ptr, type, member) (type*)((char*)(ptr) - (char*)(&(((type*)NULL)->member)))

typedef struct luvco_state {
    uv_loop_t loop;
    lua_State* main_coro;
    luvco_newluaf newluaf;
    void* newluaf_ud;
} luvco_state;

luvco_state* luvco_get_state (lua_State* L);

#define luvco_pyield(handle, L, ctx, kf) \
    (handle)->L = (L); \
    luvco_yield((L), (lua_KContext)(ctx), (kf))

void luvco_yield (lua_State *L, lua_KContext k_ctx, lua_KFunction k);

void luvco_resume (lua_State *L, int nargs);

void luvco_dump_lua_stack (lua_State *L);



#define luvco_objheader unsigned char moved : 1;

#define luvco_init_objheader(obj) (obj)->moved = false;

#define luvco_checkmoved(L, obj) \
    if ((obj)->moved) { \
        log_error("luvco obj %p is moved", (obj)); \
        luaL_error(L, "luvco obj %p is moved", (obj)); \
    }

#define luvco_moveudata(L, NL, type) { \
        type* to = luvco_pushudata_with_meta(NL, type); \
        type* from = lua_touserdata(L, -1); \
        memcpy(to, from, sizeof(type)); \
        from->moved = true; \
    }
