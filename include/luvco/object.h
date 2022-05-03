#pragma once

#define container_of(ptr, type, member) (type*)((char*)(ptr) - (char*)(&(((type*)NULL)->member)))

extern const char* LUVCO_UDATAMETA_SIZEOF_FIELD;
extern const char* LUVCO_UDATAMETA_MOVEF_FIELD;

#define luvco_new_meta(L, type) \
    luaL_newmetatable((L), "luvco."#type); \
    lua_pushvalue((L), -1); \
    lua_setfield((L), -2, "__index")

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
