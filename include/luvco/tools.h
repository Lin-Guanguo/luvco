#include <assert.h>
#include <stdbool.h>

#define luvco_new_meta(L, type)          \
    luaL_newmetatable((L), "luvco."#type);    \
    lua_pushvalue((L), -1);                   \
    lua_setfield((L), -2, "__index")

#define luvco_pushudata_with_meta(L, type) \
    (type*)lua_newuserdatauv((L), sizeof(type), 0); \
    luaL_setmetatable((L), "luvco."#type)

#define luvco_check_udata(L, n, type) \
    (type*)luaL_checkudata((L), (n), "luvco."#type)

#define luvco_push_yield_tag(L, call_back) \
    lua_pushlightuserdata(L, (void*)(call_back)); \
    lua_pushlightuserdata(L, (void*)&luvco_yield)

#define luvco_resume(L, narg, nres) \
    lua_resume((L), NULL, (narg), (nres));  \
    luvco_yield((L))

#define ASSERT_NOT_NULL(p) assert((p) != NULL)

typedef void (*luvco_yield_cb ) (lua_State *L);

// call after lua_resume a coroutine
void luvco_yield (lua_State* L);

// crate a coroutine NL and push it to L, return the NL.
//
// this function will register NL in lua_State L, keep
// it's life time until last resume, then NL will unregister
lua_State* luvco_new_co(lua_State* L);

void luvco_dump_lua_stack (lua_State *L);
