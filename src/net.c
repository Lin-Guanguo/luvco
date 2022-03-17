#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>

#include <uv.h>
#include <luvco/tools.h>
#include <luvco.h>

typedef struct ip_addr {
    struct sockaddr_storage addr;
} ip_addr;

static int new_ip_addr (lua_State* L, int v) {
    const char* ip = luaL_checkstring(L, 1);
    int port = (int)luaL_checknumber(L, 2);
    luaL_argcheck(L, port <= 65535, 2, "Invalid port number");

    ip_addr* a = luvco_pushudata_with_meta(L, ip_addr);
    if (v == 4) {
        uv_ip4_addr(ip, port, (struct sockaddr_in*)&a->addr);
    } else {
        uv_ip6_addr(ip, port, (struct sockaddr_in6*)&a->addr);
    }
    return 1;
}

static int new_ip4_addr (lua_State* L) {
    return new_ip_addr(L, 4);
}

static int new_ip6_addr (lua_State* L) {
    return new_ip_addr(L, 6);
}

typedef struct tcp_server {
    uv_tcp_t tcp;
    lua_State* waiting_accept; // lua_State which waiting for new connection, react in server_accpet_cb
} tcp_server;

typedef struct tcp_connection {
    uv_tcp_t tcp;
} tcp_connection;

void server_accept_cb (uv_stream_t* tcp, int status);

static int new_server (lua_State* L) {
    ip_addr* addr = luvco_check_udata(L, 1, ip_addr);
    luvco_state* state = luvco_get_state(L);

    tcp_server* server = luvco_pushudata_with_meta(L, tcp_server);
    int ret = uv_tcp_init(&state->loop, (uv_tcp_t *)server);
    assert(ret == 0);
    uv_tcp_bind(&server->tcp, (const struct sockaddr*)&addr->addr, 0);
    server->waiting_accept = NULL;

    printf("uv listen\n");
    uv_listen((uv_stream_t*)server, SOMAXCONN, &server_accept_cb);
    return 1;
}

static int server_accept_k (lua_State *L, int status, lua_KContext ctx) {
    return 1;
}

static int server_accept (lua_State* L) {
    tcp_server* server = luvco_check_udata(L, 1, tcp_server);
    luvco_state* state = luvco_get_state(L);

    tcp_connection* client = luvco_pushudata_with_meta(L, tcp_connection);
    int ret = uv_tcp_init(&state->loop, (uv_tcp_t *)client);
    assert(ret == 0);

    ret = uv_accept((uv_stream_t*)server, (uv_stream_t*)client);
    if (ret < 0) {
        server->waiting_accept = L;
        luvco_push_yield_tag(L, NULL);
        lua_yieldk(L, 0, (lua_KContext)NULL, server_accept_k);
    }
    return 1;
}

void server_accept_cb (uv_stream_t* tcp, int status) {
    tcp_server* server = (tcp_server*)tcp;
    if (server->waiting_accept != NULL) {
        lua_State* L = server->waiting_accept;
        server->waiting_accept = NULL;

        tcp_connection* connection = luvco_check_udata(L, -1, tcp_connection);
        int ret = uv_accept((uv_stream_t*)server, (uv_stream_t*)connection);
        assert(ret == 0);

        printf("================Accept");
        luvco_resume(L, 1, &ret);
    }
}

static const luaL_Reg net_lib [] = {
    { "new_ip4_addr", new_ip4_addr },
    { "new_ip6_addr", new_ip6_addr },
    { "new_server", new_server },
    { NULL, NULL}
};

static const luaL_Reg server_m [] = {
    { "accept", server_accept },
    { NULL, NULL}
};

int luvco_open_net (lua_State* L) {
    luvco_new_meta(L, ip_addr);
    luvco_new_meta(L, tcp_server);
    luaL_setfuncs(L, server_m, 0);
    luvco_new_meta(L, tcp_connection);

    luaL_newlib(L, net_lib);
    return 1;
}
