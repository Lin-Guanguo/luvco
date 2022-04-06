#pragma once

#define luvco_cbdata(n_ud) \
    lua_State* watting_L; luvco_lstate* watting_lstate; void* watting_ud[n_ud]

#define luvco_cbdata_set(obj, L) \
    (obj)->watting_L=(L); (obj)->watting_lstate=luvco_get_lstate((L))

#define luvco_cbdata_set1(obj, L, ud1) \
    (obj)->watting_L=(L); (obj)->watting_lstate=luvco_get_lstate((L)); \
    (obj)->watting_ud[0]=(void*)(ud1)

#define luvco_cbdata_set2(obj, L, ud1, ud2) \
    (obj)->watting_L=(L); (obj)->watting_lstate=luvco_get_lstate((L)); \
    (obj)->watting_ud[0]=(void*)(ud1); (obj)->watting_ud[1]=(void*)(ud2)

#define luvco_cbdata_set3(obj, L, ud1, ud2, ud3) \
    (obj)->watting_L=(L); (obj)->watting_lstate=luvco_get_lstate((L)); \
    (obj)->watting_ud[0]=(void*)(ud1); (obj)->watting_ud[1]=(void*)(ud2); (obj)->watting_ud[2]=(void*)(ud3)

#define luvco_cbdata_clear(obj) \
    (obj)->watting_L=NULL; (obj)->watting_lstate=NULL; \
    for(int i = 0; i < sizeof((obj)->watting_ud) / sizeof(void*); ++i) (obj)->watting_ud[i]=NULL

#define luvco_toresume_incb(obj, nargs) \
    luvco_toresume((obj)->watting_lstate, (obj)->watting_L, (nargs))


typedef struct luvco_objhead {
    bool moved;
} luvco_objhead;

#define luvco_init_objheader(head) (head)->moved = false;

#define luvco_checkmoved(L, head) \
    if ((head)->moved) { \
        log_error("luvco obj %p is moved", (head)); \
        luaL_error(L, "luvco obj %p is moved", (head)); \
    }
