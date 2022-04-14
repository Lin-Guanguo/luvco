#pragma once

extern const char* LUVCO_UDATAMETA_SIZEOF_FIELD;
extern const char* LUVCO_UDATAMETA_MOVEABLE_FIELD;

#define luvco_new_meta(L, type) \
    luaL_newmetatable((L), "luvco."#type); \
    lua_pushvalue((L), -1); \
    lua_setfield((L), -2, "__index"); \
    lua_pushinteger((L), sizeof(type)); \
    lua_setfield(L, -2, LUVCO_UDATAMETA_SIZEOF_FIELD)

#define luvco_new_meta_moveable(L, type) \
    luaL_newmetatable((L), "luvco."#type); \
    lua_pushvalue((L), -1); \
    lua_setfield((L), -2, "__index"); \
    lua_pushinteger((L), sizeof(type)); \
    lua_setfield(L, -2, LUVCO_UDATAMETA_SIZEOF_FIELD) \
    lua_pushboolean((L), true) \
    lua_setfield(L, -2, LUVCO_UDATAMETA_MOVEABLE_FIELD)

#define luvco_pushudata_with_meta(L, type) \
    (type*)lua_newuserdatauv((L), sizeof(type), 0); \
    luaL_setmetatable((L), "luvco."#type)

#define luvco_check_udata(L, n, type) \
    (type*)luaL_checkudata((L), (n), "luvco."#type)



#define luvco_cbdata(n_ud) \
    lua_State* waiting_L; luvco_lstate* waiting_lstate; void* waiting_ud[n_ud]

#define luvco_cbdata_set(obj, L) \
    (obj)->waiting_L=(L); (obj)->waiting_lstate=luvco_get_lstate((L))

#define luvco_cbdata_set1(obj, L, ud1) \
    (obj)->waiting_L=(L); (obj)->waiting_lstate=luvco_get_lstate((L)); \
    (obj)->waiting_ud[0]=(void*)(ud1)

#define luvco_cbdata_set2(obj, L, ud1, ud2) \
    (obj)->waiting_L=(L); (obj)->waiting_lstate=luvco_get_lstate((L)); \
    (obj)->waiting_ud[0]=(void*)(ud1); (obj)->waiting_ud[1]=(void*)(ud2)

#define luvco_cbdata_set3(obj, L, ud1, ud2, ud3) \
    (obj)->waiting_L=(L); (obj)->waiting_lstate=luvco_get_lstate((L)); \
    (obj)->waiting_ud[0]=(void*)(ud1); (obj)->waiting_ud[1]=(void*)(ud2); (obj)->waiting_ud[2]=(void*)(ud3)

#define luvco_cbdata_clear(obj) \
    (obj)->waiting_L=NULL; (obj)->waiting_lstate=NULL; \
    for(int i = 0; i < sizeof((obj)->waiting_ud) / sizeof(void*); ++i) (obj)->waiting_ud[i]=NULL

#define luvco_toresume_incb(obj, nargs) \
    luvco_toresume((obj)->waiting_lstate, (obj)->waiting_L, (nargs))



typedef struct luvco_objhead_moveable {
    bool moved;
} luvco_objhead_moveable;

#define luvco_init_objhead(head) (head)->moved = false;

#define luvco_checkmoved(L, head) \
    if (((luvco_objhead_moveable*)(head))->moved) { \
        log_error("luvco obj %p is moved, can't use", (head)); \
        luaL_error(L, "luvco obj %p is moved, can't use", (head)); \
    }
