
#define luvco_new_meta(L, type)          \
    luaL_newmetatable((L), "luvco."#type);    \
    lua_pushvalue((L), -1);                   \
    lua_setfield((L), -2, "__index")

#define luvco_pushudata_with_meta(L, type) \
    (type*)lua_newuserdatauv((L), sizeof(type), 0); \
    luaL_setmetatable((L), "luvco."#type)

#define luvco_check_udata(L, n, type) \
    (type*)luaL_checkudata((L), (n), "luvco."#type)

#define push_async_yield_tag(L, async_fn_id) \
    lua_pushinteger(L, THIS_LIB_ASYNC_YIELD_ID + (async_fn_id)); \
    lua_pushlightuserdata(L, (void*)&luvco_yield_tag)

#define luvco_resume(L, narg, nres) \
    lua_resume((L), NULL, (narg), (nres));  \
    luvco_yield((L))

void luvco_dump_lua_stack (lua_State *L);

void luvco_yield_tag();