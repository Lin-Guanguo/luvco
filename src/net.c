#include <stdlib.h>

#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>

#include <uv.h>
#include <luvco.h>
#include <luvco/tools.h>

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
    unsigned char closed : 1;

    uv_tcp_t tcp;
    lua_State* L; // lua_State which waiting for new connection, react in server_accpet_cb
} tcp_server;

typedef struct tcp_connection {
    unsigned char closed : 1;

    uv_tcp_t tcp;
    lua_State* L; // for resume, thus before call yield, update this to current coroutine

    char* read_buf; // read buf, alloc in first read, free in gc

    uv_write_t* write_req;
    uv_buf_t* write_bufs; // array of buf, wait to write, alloc in write, free in gc
    size_t write_bufs_n;
} tcp_connection;

static void server_accept_cb (uv_stream_t* tcp, int status);

static int new_server (lua_State* L) {
    ip_addr* addr = luvco_check_udata(L, 1, ip_addr);
    luvco_state* state = luvco_get_state(L);

    tcp_server* server = luvco_pushudata_with_meta(L, tcp_server);
    server->L = NULL;
    server->closed = false;

    int ret = uv_tcp_init(&state->loop, &server->tcp);
    assert(ret == 0);
    uv_tcp_bind(&server->tcp, (const struct sockaddr*)&addr->addr, 0);

    log_trace("server %p start listen", server);
    uv_listen((uv_stream_t*)&server->tcp, SOMAXCONN, &server_accept_cb);
    return 1;
}

static void server_accept_cb (uv_stream_t* tcp, int status) {
    tcp_server* server = container_of(tcp, tcp_server, tcp);
    log_trace("server %p accept cb called, waiting=%p", server, server->L);
    if (server->L != NULL) {
        lua_State* L = server->L;
        server->L = NULL;

        tcp_connection* con = luvco_check_udata(L, -1, tcp_connection);
        int ret = uv_accept((uv_stream_t*)&server->tcp, (uv_stream_t*)&con->tcp);
        assert(ret == 0);

        con->closed = false;
        log_trace("accept connection %p", con);
        luvco_resume(L, 1);
    }
}

static int server_accept_k (lua_State *L, int status, lua_KContext ctx);

// return connetion when succeed
// return nil if server already closed or close when watting accept
static int server_accept (lua_State* L) {
    tcp_server* server = luvco_check_udata(L, 1, tcp_server);
    luvco_state* state = luvco_get_state(L);

    // if closed, return nil when accept
    if (server->closed) {
        lua_pushnil(L);
        return 1;
    }

    tcp_connection* client = luvco_pushudata_with_meta(L, tcp_connection);
    client->closed = true;
    client->read_buf = NULL;
    client->write_req = NULL;
    client->write_bufs = NULL;
    client->write_bufs_n = 0;
    int ret = uv_tcp_init(&state->loop, (uv_tcp_t *)&client->tcp);
    assert(ret == 0);

    ret = uv_accept((uv_stream_t*)&server->tcp, (uv_stream_t*)&client->tcp);
    if (ret < 0) {
        log_trace("server %p accpet no income, wait...", server);
        luvco_pyield(server, L, NULL, server_accept_k);
    }
    client->closed = false;
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
        server->closed = true;
        log_trace("server %p closed", server);
        uv_close((uv_handle_t*)&server->tcp, server_close_cb);

        // close when accept directly return
        if (server->L != NULL) {
            lua_pushnil(server->L);
            luvco_resume(server->L, 1);
        }
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

// return string when read succeed
// return "" when read EOF
// return nil when some error happen
static int connection_read (lua_State* L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    log_trace("connection %p read", con);

    int ret = uv_read_start((uv_stream_t*)&con->tcp, connection_alloc_cb, connection_read_cb);
    assert(ret == 0);
    luvco_pyield(con, L, NULL, connection_read_k);
}

static int connection_read_k (lua_State *L, int status, lua_KContext ctx) {
    return 1;
}

static void connection_alloc_cb (uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    tcp_connection* con = container_of(handle, tcp_connection, tcp);
    if (con->read_buf == NULL) {
        con->read_buf = (char*)malloc(suggested_size);
    }
    buf->base = con->read_buf;
    buf->len = suggested_size;
}

static void connection_read_cb (uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    uv_read_stop(stream); // one shot read

    tcp_connection* con = container_of(stream, tcp_connection, tcp);
    lua_State* L = con->L;
    if (nread > 0) {
        lua_pushlstring(L, buf->base, nread);
    } else if (nread == UV_EOF) {
        log_trace("connection %p read eof", con);
        lua_pushstring(L, "");
    } else {
        log_warn("connection %p read, some error happen, nread=%ld", con, nread);
        lua_pushnil(L);
    }
    luvco_resume(L, 1);
}

static void connection_write_cb (uv_write_t* req, int status);
static int connection_write_k (lua_State *L, int status, lua_KContext ctx);

// return 0 when write succeeded, <0 otherwise
static int connection_write (lua_State* L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    log_trace("connection %p write", con);

    int top = lua_gettop(L);
    size_t write_n = top - 1;
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
        assert(con->write_req != NULL);
    }
    int ret = uv_write(con->write_req, (uv_stream_t*)&con->tcp, con->write_bufs, write_n, connection_write_cb);
    assert(ret == 0);
    luvco_pyield(con, L, NULL, connection_write_k);
}

static int connection_write_k (lua_State *L, int status, lua_KContext ctx) {
    return 1;
}

static void connection_write_cb (uv_write_t* req, int status) {
    tcp_connection* con = container_of(req->handle, tcp_connection, tcp);
    if (status != 0) {
        log_error("connection %p write error, status %d", con, status);
    }
    lua_State* L = con->L;
    lua_pushinteger(L, status);
    luvco_resume(L, 1);
}

static int connection_close_k (lua_State *L, int status, lua_KContext ctx);
static void connection_close_cb (uv_handle_t* handle);

// close connection
// close multi times is ok
static int connection_close (lua_State *L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    if (!con->closed) {
        log_trace("connection %p close", con);
        uv_close((uv_handle_t *)&con->tcp, connection_close_cb);
        con->closed = true;
        luvco_pyield(con, L, NULL, connection_close_k);
    }
    return 0;
}

static int connection_close_k (lua_State *L, int status, lua_KContext ctx) {
    return 0;
}

static void connection_close_cb (uv_handle_t* handle) {
    tcp_connection* con = container_of(handle, tcp_connection, tcp);
    luvco_resume(con->L, 0);
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
