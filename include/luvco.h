#include <lua/lua.h>

struct luvco_lib_info
{
    const char* name;
    int async_fn_id;
};

extern const struct luvco_lib_info LUVCO_LIB_INFO [2];

int luvco_run (lua_State* L);

int luvco_open_base (lua_State* L);

