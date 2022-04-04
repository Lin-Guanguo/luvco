#include <lua/lua.h>
#include <lua/lualib.h>
#include <lua/lauxlib.h>

const char* luvco_metadata_sizeof_record = "__sizeof";

void luvco_dump_lua_stack (lua_State* L) {
  int top=lua_gettop(L);
  printf("L: %p dumpstack: \n", L);
  for (int i=1; i <= top; i++) {
    printf("\t%d\t%s\t", i, luaL_typename(L,i));
    switch (lua_type(L, i)) {
      case LUA_TNUMBER:
        printf("\t%g\n",lua_tonumber(L,i));
        break;
      case LUA_TSTRING:
        printf("\t%s\n",lua_tostring(L,i));
        break;
      case LUA_TBOOLEAN:
        printf("\t%s\n", (lua_toboolean(L, i) ? "true" : "false"));
        break;
      case LUA_TNIL:
        printf("\t%s\n", "nil");
        break;
      case LUA_TUSERDATA:
        luaL_getmetafield(L, i, "__name");
        const char* name = lua_tostring(L, -1);
        printf("\t%s\n", name);
        lua_pop(L, 1);
        break;
      default:
        printf("\t%p\n", lua_topointer(L,i));
        break;
    }
  }
}
