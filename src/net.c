#include <luvco.h>
#include <luvco/base.h>
#include <luvco/scheduler.h>
#include <luvco/object.h>

#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>
#include <uv.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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

static int ip_addr_info (lua_State* L) {
    ip_addr* a = luvco_check_udata(L, 1, ip_addr);
    char hoststr[NI_MAXHOST];
    char portstr[NI_MAXSERV];
    int rc = getnameinfo((struct sockaddr *)&a->addr, sizeof(a->addr), hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc == 0) {
        lua_pushstring(L, hoststr);
        lua_pushstring(L, portstr);
        return 2;
    } else {
        return 0;
    }
}

static void ip_addr_move (void* from, void* to) {
    memcpy(to, from, sizeof(ip_addr));
}

typedef struct tcp_connection tcp_connection;

typedef struct tcp_server {
    uv_tcp_t tcp;
    bool closed;

    struct {
        luvco_cbdata;
        ip_addr* addr;
    } new;

    struct {
        luvco_cbdata;
        tcp_connection* con;
    } accept;

    struct {
        luvco_cbdata;
    } close;
} tcp_server;

typedef struct tcp_connection {
    uv_tcp_t tcp;
    bool closed;

    struct {
        char* buf; // read buf, alloc in first read, free in gc
        luvco_cbdata;
        size_t nread;
    } read;

    struct {
        uv_write_t* req;
        uv_buf_t* bufs; // array of buf, wait to write, alloc in write, free in gc
        size_t bufs_n;
        luvco_cbdata;
        union {
            unsigned int write_n;   // args
            int status;             // return
        } u;
    } write;

    struct {
        luvco_cbdata;
    } close;
} tcp_connection;

static int new_server_k (lua_State *L, int status, lua_KContext ctx) {
    return 1;
}

static void server_accept_cb (uv_stream_t* tcp, int status);

static void new_server_uvwork (luvco_uvwork* uvwork) {
    tcp_server* server = container_of(uvwork, tcp_server, new.uv);
    luvco_cbdata_extract(server->new);
    ip_addr* addr = server->new.addr;

    int ret = uv_tcp_init(&gstate->loop, &server->tcp);
    assert(ret == 0);
    uv_tcp_bind(&server->tcp, (const struct sockaddr*)&addr->addr, 0);

    log_trace("server %p start listen", server);
    server->accept.L = NULL;
    uv_listen((uv_stream_t*)&server->tcp, SOMAXCONN, &server_accept_cb);

    luvco_toresume(lstate, L, 1);
}

static void server_accept_uvwork (luvco_uvwork* uvwork);
static void server_close_uvwork (luvco_uvwork* uvwork);

static int new_server (lua_State* L) {
    ip_addr* addr = luvco_check_udata(L, 1, ip_addr);
    luvco_lstate* lstate = luvco_get_state(L);
    luvco_gstate* gstate = lstate->gstate;

    tcp_server* server = luvco_pushudata_with_meta(L, tcp_server);
    server->closed = false;
    server->new.uv.cb = new_server_uvwork;
    server->accept.uv.cb = server_accept_uvwork;
    server->close.uv.cb = server_close_uvwork;

    luvco_cbdata_set(server->new, L, lstate);
    server->new.addr = addr;
    luvco_add_uvwork(gstate, &server->new.uv);
    luvco_yield(L, (lua_KContext)NULL, new_server_k);
}

static int server_accept_k (lua_State *L, int status, lua_KContext ctx) {
    log_trace("accept connection %p", (void*)ctx);
    return 1;
}

static void server_accept_cb (uv_stream_t* tcp, int status) {
    tcp_server* server = container_of(tcp, tcp_server, tcp);
    log_trace("server %p accept cb called, waiting_lstate=%p", server, server->new.L);
    if (server->accept.L != NULL) {
        luvco_cbdata_extract(server->accept);
        tcp_connection* con = server->accept.con;
        server->accept.L = NULL;

        int ret = uv_accept((uv_stream_t*)&server->tcp, (uv_stream_t*)&con->tcp);
        assert(ret == 0);

        con->closed = false;
        luvco_toresume(lstate, L, 1); // one res is connection, push in accept
    }
}

static void server_accept_uvwork (luvco_uvwork* uvwork) {
    tcp_server* server = container_of(uvwork, tcp_server, accept.uv);
    luvco_cbdata_extract(server->accept);
    tcp_connection* client = server->accept.con;

    int ret = uv_tcp_init(&gstate->loop, (uv_tcp_t *)&client->tcp);
    assert(ret == 0);

    ret = uv_accept((uv_stream_t*)&server->tcp, (uv_stream_t*)&client->tcp);
    if (ret < 0) {
        luvco_cbdata_set(server->accept, L, lstate);
        server->accept.con = client;
    } else {
        luvco_toresume(lstate, L, 1);
    }
}

static void connection_read_uvwork(luvco_uvwork* uvwork);
static void connection_write_uvwork(luvco_uvwork* uvwork);
static void connection_close_uvwork(luvco_uvwork* uvwork);

// return connetion when succeed
// return nil if server already closed or close when waiting accept
static int server_accept (lua_State* L) {
    tcp_server* server = luvco_check_udata(L, 1, tcp_server);
    luvco_lstate* lstate = luvco_get_state(L);
    luvco_gstate* gstate = lstate->gstate;

    // if closed, return nil when accept
    if (server->closed) {
        lua_pushnil(L);
        return 1;
    }

    tcp_connection* client = luvco_pushudata_with_meta(L, tcp_connection);
    client->read.uv.cb = connection_read_uvwork;
    client->write.uv.cb = connection_write_uvwork;
    client->close.uv.cb = connection_close_uvwork;
    client->closed = false;
    client->read.buf = NULL;
    client->write.req = NULL;
    client->write.bufs = NULL;
    client->write.bufs_n = 0;

    luvco_cbdata_set(server->accept, L, lstate);
    server->accept.con = client;
    luvco_add_uvwork(gstate, &server->accept.uv);
    luvco_yield(L, (lua_KContext)NULL, server_accept_k);
}

static int server_close_k (lua_State *L, int status, lua_KContext ctx) {
    return 0;
}

static void server_close_cb (uv_handle_t* handle) {
    tcp_server* server = container_of(handle, tcp_server, tcp);
    luvco_toresume_incb(server->close, 0);
}

static void server_close_uvwork (luvco_uvwork* uvwork) {
    tcp_server* server = container_of(uvwork, tcp_server, close.uv);
    uv_close((uv_handle_t*)&server->tcp, server_close_cb);
}

static int server_close (lua_State* L) {
    tcp_server* server = luvco_check_udata(L, 1, tcp_server);
    luvco_lstate* lstate = luvco_get_state(L);
    luvco_gstate* gstate = lstate->gstate;
    if (!server->closed) {
        server->closed = true;
        log_trace("server %p closed", server);

        // close when accept directly return
        if (server->accept.L != NULL) {
            log_trace("server %p closed, resume waiting accept %p", server, server->accept.L);
            lua_settop(L, 0);
            lua_pushnil(server->accept.L);
            luvco_toresume_incb(server->accept, 1);
        }

        luvco_cbdata_set(server->close, L, lstate);
        luvco_add_uvwork(gstate, &server->close.uv);
        luvco_yield(L, (lua_KContext)NULL, server_close_k);
    }
    return 0;
}

static int server_gc (lua_State* L) {
    tcp_server* server = luvco_check_udata(L, 1, tcp_server);
    log_trace("server %p gc", server);
    server_close(L);
    return 0;
}

static void connection_alloc_cb (uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    tcp_connection* con = container_of(handle, tcp_connection, tcp);
    if (con->read.buf == NULL) {
        con->read.buf = (char*)malloc(suggested_size);
    }
    buf->base = con->read.buf;
    buf->len = suggested_size;
}

static int connection_read_k (lua_State *L, int status, lua_KContext ctx) {
    tcp_connection* con = (tcp_connection*)ctx;
    ssize_t nread = (ssize_t)con->read.nread;
    char* buf = con->read.buf;
    if (nread > 0) {
        lua_pushlstring(L, buf, nread);
    } else if (nread == UV_EOF) {
        log_trace("connection %p read eof", con);
        lua_pushstring(L, "");
    } else {
        log_warn("connection %p read, some error happen, nread=%ld", con, nread);
        lua_pushnil(L);
    }
    return 1;
}

static void connection_read_cb (uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    uv_read_stop(stream); // one shot read
    tcp_connection* con = container_of(stream, tcp_connection, tcp);
    // log_trace("connection read cb called %p", con);

    con->read.nread = nread;
    luvco_toresume_incb(con->read, 0);
}

static void connection_read_uvwork(luvco_uvwork* uvwork) {
    tcp_connection* con = container_of(uvwork, tcp_connection, read.uv);
    int ret = uv_read_start((uv_stream_t*)&con->tcp, connection_alloc_cb, connection_read_cb);
    assert(ret == 0);
}

// return string when read succeed
// return "" when read EOF
// return nil when some error happen
static int connection_read (lua_State* L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    luvco_lstate* lstate = luvco_get_state(L);
    luvco_gstate* gstate = lstate->gstate;
    log_trace("connection %p read", con);

    luvco_cbdata_set(con->read, L, lstate);
    luvco_add_uvwork(gstate, &con->read.uv);
    luvco_yield(L, (lua_KContext)con, connection_read_k);
}

static int connection_write_k (lua_State *L, int status, lua_KContext ctx) {
    tcp_connection* con = (tcp_connection*)ctx;
    int write_status = (int)(intptr_t)con->write.u.status;
    if (write_status != 0) {
        log_error("connection %p write error, status %d", con, status);
    }
    lua_pushinteger(L, status);
    return 1;
}

static void connection_write_cb (uv_write_t* req, int status) {
    tcp_connection* con = container_of(req->handle, tcp_connection, tcp);
    con->write.u.status = status;
    luvco_toresume_incb(con->write, 0);
}

static void connection_write_uvwork(luvco_uvwork* uvwork) {
    tcp_connection* con = container_of(uvwork, tcp_connection, write.uv);
    size_t write_n = (size_t)con->write.u.write_n;
    int ret = uv_write(con->write.req, (uv_stream_t*)&con->tcp, con->write.bufs, write_n, connection_write_cb);
    assert(ret == 0);
}

// return 0 when write succeeded, <0 otherwise
static int connection_write (lua_State* L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    luvco_lstate* lstate = luvco_get_state(L);
    luvco_gstate* gstate = lstate->gstate;
    log_trace("connection %p write", con);

    int top = lua_gettop(L);
    size_t write_n = top - 1;
    if (con->write.bufs_n < write_n) {
        con->write.bufs = (uv_buf_t*)realloc(con->write.bufs, sizeof(uv_buf_t) * write_n);
        con->write.bufs_n = write_n;
    }
    for (int i = 2; i <= top; ++i) {
        size_t len;
        char* s = (char*)luaL_checklstring(L, i, &len);
        con->write.bufs[i-2].base = s;
        con->write.bufs[i-2].len = len;
    }
    if (con->write.req == NULL) {
        con->write.req = (uv_write_t*)malloc(sizeof(uv_write_t));
        assert(con->write.req != NULL);
    }

    con->write.u.write_n = write_n;
    luvco_cbdata_set(con->write, L, lstate);
    luvco_add_uvwork(gstate, &con->write.uv);
    luvco_yield(L, (lua_KContext)con, connection_write_k);
}

static int connection_close_k (lua_State *L, int status, lua_KContext ctx) {
    return 0;
}

static void connection_close_cb (uv_handle_t* handle) {
    tcp_connection* con = container_of(handle, tcp_connection, tcp);
    luvco_toresume_incb(con->close, 0);
}

static void connection_close_uvwork(luvco_uvwork* uvwork) {
    tcp_connection* con = container_of(uvwork, tcp_connection, close.uv);
    uv_close((uv_handle_t *)&con->tcp, connection_close_cb);
}

// close connection
// close multi times is ok
static int connection_close (lua_State *L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    luvco_lstate* lstate = luvco_get_state(L);
    luvco_gstate* gstate = lstate->gstate;
    if (!con->closed) {
        log_trace("connection %p close", con);
        con->closed = true;
        // TODO: reading writding check

        luvco_cbdata_set(con->close, L, lstate);
        luvco_add_uvwork(gstate, &con->close.uv);
        luvco_yield(L, (lua_KContext)con, connection_close_k);
    }
    return 0;
}

static int connection_gc (lua_State *L) {
    tcp_connection* con = luvco_check_udata(L, 1, tcp_connection);
    log_trace("connection %p gc", con);
    connection_close(L);
    free(con->read.buf);
    free(con->write.req);
    free(con->write.bufs);
    return 0;
}

static const luaL_Reg net_lib [] = {
    { "new_ip4_addr", new_ip4_addr },
    { "new_ip6_addr", new_ip6_addr },
    { "new_server", new_server },
    { NULL, NULL}
};

static const luaL_Reg ip_addr_m [] = {
    { "info", ip_addr_info },
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
    luvco_new_meta_moveable(L, ip_addr, ip_addr_move);
    luaL_setfuncs(L, ip_addr_m, 0);
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
