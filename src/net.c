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
    bool closed;
} tcp_server;

typedef struct tcp_connection {
    uv_tcp_t tcp;
    lua_State* L; // for resume, thus before call yield, update this to current coroutine
    bool closed;

    char* read_buf; // read buf, alloc in first read, free in gc

    uv_write_t* write_req;
    uv_buf_t* write_bufs; // array of buf, wait to write, alloc in write, free in gc
    int write_bufs_n;
} tcp_connection;

static void server_accept_cb (uv_stream_t* tcp, int status);

static int new_server (lua_State* L) {
    ip_addr* addr = luvco_check_udata(L, 1, ip_addr);
    luvco_state* state = luvco_get_state(L);

    tcp_server* server = luvco_pushudata_with_meta(L, tcp_server);
    server->waiting_accept = NULL;
    server->closed = false;

    int ret = uv_tcp_init(&state->loop, (uv_tcp_t *)server);
    assert(ret == 0);
    uv_tcp_bind(&server->tcp, (const struct sockaddr*)&addr->addr, 0);

    log_trace("server %p start listen", server);
    uv_listen((uv_stream_t*)server, SOMAXCONN, &server_accept_cb);
    return 1;
}

static void server_accept_cb (uv_stream_t* tcp, int status) {
    tcp_server* server = (tcp_server*)tcp;
    log_trace("server %p accept cb called, waiting=%p", server, server->waiting_accept);
    if (server->waiting_accept != NULL) {
        lua_State* L = server->waiting_accept;
        server->waiting_accept = NULL;

        tcp_connection* connection = luvco_check_udata(L, -1, tcp_connection);
        int ret = uv_accept((uv_stream_t*)server, (uv_stream_t*)connection);
        assert(ret == 0);

        log_trace("accept connection %p", connection);
        luvco_resume(L, 1);
    }
}

static int server_accept_k (lua_State *L, int status, lua_KContext ctx);

static int server_accept (lua_State* L) {
    tcp_server* server = luvco_check_udata(L, 1, tcp_server);
    luvco_state* state = luvco_get_state(L);

    tcp_connection* client = luvco_pushudata_with_meta(L, tcp_connection);
    client->closed = false;
    client->read_buf = NULL;
    client->write_req = NULL;
    client->write_bufs = NULL;
    client->write_bufs_n = 0;
    int ret = uv_tcp_init(&state->loop, (uv_tcp_t *)client);
    assert(ret == 0);

    ret = uv_accept((uv_stream_t*)server, (uv_stream_t*)client);
    if (ret < 0) {
        server->waiting_accept = L;
        log_trace("server %p accpet no income, wait...", server);
        lua_yieldk(L, 0, (lua_KContext)NULL, server_accept_k);
    }
    log_trace("accept connection %p", client);
    return 1;
}

static int server_accept_k (lua_State *L, int status, lua_KContext ctx) {
    return 1;
}

static void server_close_cb (uv_handle_t* handle);

static int server_close (lua_State* L) {
    tcp_server* server = luvco_check_udata(L, 1, tcp_server);
    if (!server->closed) {
        log_trace("server %p closed", server);
        uv_close((uv_handle_t*)server, server_close_cb);
        server->closed = 1;
    }
    return 0;
}

static void server_close_cb (uv_handle_t* handle) {
    /* do nothing, memory in handled by lua vm */
}

static int server_gc (lua_State* L) {
    tcp_server* server = luvco_check_udata(L, 1, tcp_server);
    log_trace("server %p gc", server);
    server_close(L);
    return 0;
}

static void connection_alloc_cb (uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void connection_read_cb (uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static int connection_read_k (lua_State *L, int status, lua_KContext ctx);

static int connection_read (lua_State* L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    con->L = L;
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
        luvco_resume(L, 1);
    }
    // TODO
}

static void connection_write_cb (uv_write_t* req, int status);
static int connection_write_k (lua_State *L, int status, lua_KContext ctx);

static int connection_write (lua_State* L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    con->L = L;
    log_trace("connection %p write", con);

    int top = lua_gettop(L);
    int write_n = top - 1;
    if (con->write_bufs_n < write_n) {
        con->write_bufs = (uv_buf_t*)realloc(con->write_bufs, sizeof(uv_buf_t) * write_n);
        con->write_bufs_n = write_n;
    }
    for (int i = 2; i <= top; ++i) {
        size_t len;
        char* s = (char*)luaL_checklstring(L, i, &len);
        con->write_bufs[i-2].base = s;
        con->write_bufs[i-2].len = len;
    }
    if (con->write_req == NULL) {
        con->write_req = (uv_write_t*)malloc(sizeof(uv_write_t));
        ASSERT_NOT_NULL(con->write_req);
    }
    int ret = uv_write(con->write_req, (uv_stream_t*)con, con->write_bufs, write_n, connection_write_cb);
    assert(ret == 0);
    lua_yieldk(L, 0, (lua_KContext)NULL, connection_write_k);
}

static int connection_write_k (lua_State *L, int status, lua_KContext ctx) {
    return 1;
}

static void connection_write_cb (uv_write_t* req, int status) {
    tcp_connection* con = (tcp_connection*)req->handle;
    if (status != 0) {
        log_error("connection %p write error, status %d", con, status);
    }
    lua_State* L = con->L;
    lua_pushinteger(L, status);
    luvco_resume(L, 1);
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
    /* do nothing, memory in handled by lua vm */
}

static int connection_gc (lua_State *L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    log_trace("connection %p gc", con);
    connection_close(L);
    free(con->read_buf);
    free(con->write_req);
    free(con->write_bufs);
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
    { "close", server_close },
    { "__gc", server_gc },
    { NULL, NULL}
};

static const luaL_Reg con_m [] = {
    { "read", connection_read },
    { "write", connection_write },
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
