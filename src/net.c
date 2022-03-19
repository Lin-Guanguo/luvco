#include <stdlib.h>

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
    lua_State* L;
    char* read_buf;
    bool closed;
} tcp_connection;

static void server_accept_cb (uv_stream_t* tcp, int status);

static int new_server (lua_State* L) {
    ip_addr* addr = luvco_check_udata(L, 1, ip_addr);
    luvco_state* state = luvco_get_state(L);

    tcp_server* server = luvco_pushudata_with_meta(L, tcp_server);
    int ret = uv_tcp_init(&state->loop, (uv_tcp_t *)server);
    assert(ret == 0);
    uv_tcp_bind(&server->tcp, (const struct sockaddr*)&addr->addr, 0);
    server->waiting_accept = NULL;

    log_trace("server %p start listen", server);
    uv_listen((uv_stream_t*)server, SOMAXCONN, &server_accept_cb);
    return 1;
}

static void server_accept_cb (uv_stream_t* tcp, int status) {
    tcp_server* server = (tcp_server*)tcp;
    if (server->waiting_accept != NULL) {
        lua_State* L = server->waiting_accept;
        server->waiting_accept = NULL;

        tcp_connection* connection = luvco_check_udata(L, -1, tcp_connection);
        int ret = uv_accept((uv_stream_t*)server, (uv_stream_t*)connection);
        assert(ret == 0);

        luvco_resume(L, 1, &ret);
    }
}

static int server_accept_k (lua_State *L, int status, lua_KContext ctx);

static int server_accept (lua_State* L) {
    tcp_server* server = luvco_check_udata(L, 1, tcp_server);
    luvco_state* state = luvco_get_state(L);
    log_trace("server %p accept", server);

    tcp_connection* client = luvco_pushudata_with_meta(L, tcp_connection);
    client->L = L;
    client->read_buf = NULL;
    client->closed = false;
    int ret = uv_tcp_init(&state->loop, (uv_tcp_t *)client);
    assert(ret == 0);

    ret = uv_accept((uv_stream_t*)server, (uv_stream_t*)client);
    if (ret < 0) {
        server->waiting_accept = L;
        lua_yieldk(L, 0, (lua_KContext)NULL, server_accept_k);
    }
    return 1;
}

static int server_accept_k (lua_State *L, int status, lua_KContext ctx) {
    return 1;
}

static void connection_alloc_cb (uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void connection_read_cb (uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static int connection_read_k (lua_State *L, int status, lua_KContext ctx);

static int connection_read (lua_State* L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    luvco_state* state = luvco_get_state(L);
    log_trace("connection %p read", con);

    int ret = uv_read_start((uv_stream_t*)con, connection_alloc_cb, connection_read_cb);
    assert(ret == 0);
    lua_yieldk(L, 0, (lua_KContext)NULL, connection_read_k);
}

static int connection_read_k (lua_State *L, int status, lua_KContext ctx) {
    return 1;
}

static void connection_alloc_cb (uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    tcp_connection* con = (tcp_connection*)handle;
    if (con->read_buf == NULL) {
        con->read_buf = (char*)malloc(suggested_size);
    }
    buf->base = con->read_buf;
    buf->len = suggested_size;
}

static void connection_read_cb (uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    uv_read_stop(stream); // one shot read

    tcp_connection* con = (tcp_connection*)stream;
    lua_State* L = con->L;
    if (nread > 0) {
        lua_pushlstring(L, buf->base, nread);
        int res;
        luvco_resume(L, 1, &res);
    }
    // TODO
}

static void connection_close_cb (uv_handle_t* handle);

static int connection_close (lua_State *L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    if (!con->closed) {
        log_trace("connection %p close", con);
        uv_close((uv_handle_t *)con, connection_close_cb);
        con->closed = true;
    }
    return 0;
}

static void connection_close_cb (uv_handle_t* handle) {
    /* do nothing, memory in handler by lua vm */
}

static int connection_gc (lua_State *L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    log_trace("connection %p gc", con);
    connection_close(L);
    free(con->read_buf);
    return 0;
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

static const luaL_Reg con_m [] = {
    { "read", connection_read },
    { "close", connection_close },
    { "__gc", connection_gc },
    { NULL, NULL}
};

int luvco_open_net (lua_State* L) {
    luvco_new_meta(L, ip_addr);
    luvco_new_meta(L, tcp_server);
    luaL_setfuncs(L, server_m, 0);
    luvco_new_meta(L, tcp_connection);
    luaL_setfuncs(L, con_m, 0);

    int ty = lua_getglobal(L, "luvco");
    luaL_newlib(L, net_lib);
    if (ty == LUA_TTABLE) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "net");
    }
    return 1;
}
