#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>

#include <uv.h>
#include <luvco/tools.h>

typedef struct ip_addr {
    struct sockaddr_storage addr;
} ip_addr;

static int luvco_new_ip_addr (lua_State* L, int v) {
    const char* ip = luaL_checkstring(L, 1);
    int port = (int)luaL_checknumber(L, 2);
    luaL_argcheck(L, port > 65535, 2, "Invalid port number");

    ip_addr* a = luvco_pushudata_with_meta(L, ip_addr);
    if (v == 4) {
        uv_ip4_addr(ip, port, (struct sockaddr_in*)&a->addr);
    } else {
        uv_ip6_addr(ip, port, (struct sockaddr_in6*)&a->addr);
    }
    return 1;
}

static int luvco_new_ip4_addr (lua_State* L) {
    return luvco_new_ip_addr(L, 4);
}

static int luvco_new_ip6_addr (lua_State* L) {
    return luvco_new_ip_addr(L, 6);
}

typedef struct tcp_server {
    uv_tcp_t server;
    lua_State* L;       // coroutine to resume
} tcp_server;

static int luvco_new_server (lua_State* L) {
    ip_addr* addr = luvco_check_udata(L, 1, ip_addr);
    tcp_server* server = luvco_pushudata_with_meta(L, tcp_server);

    uv_tcp_bind(&server->server, (const struct sockaddr*)&addr->addr, 0);
    server->L = L;
    return 1;
}

// TODO: listen

static const luaL_Reg net_lib [] = {
    { "new_ip4_addr", luvco_new_ip4_addr },
    { "new_ip6_addr", luvco_new_ip6_addr },
    { NULL, NULL}
};

int luvco_open_net (lua_State* L) {
    luvco_new_meta(L, ip_addr);
    luvco_new_meta(L, tcp_server);

    luaL_newlib(L, net_lib);
    return 1;
}
