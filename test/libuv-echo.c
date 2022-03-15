#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define ASSERT(x) assert(x)
#define ASSERT_NOT_NULL(x) assert((x) != NULL)
#define ASSERT_EQ(x, y) assert((x) == (y))
#define FATAL(x) abort()

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
} write_req_t;

static uv_loop_t *loop;

static int server_closed;
static uv_tcp_t tcpServer;
static uv_udp_t udpServer;
static uv_pipe_t pipeServer;
static uv_handle_t *server;
static uv_udp_send_t *send_freelist;

static void after_write(uv_write_t *req, int status);
static void after_read(uv_stream_t *, ssize_t nread, const uv_buf_t *buf);
static void on_close(uv_handle_t *peer);
static void on_server_close(uv_handle_t *handle);
static void on_connection(uv_stream_t *, int status);

static void after_write(uv_write_t *req, int status) {
    write_req_t *wr;

    /* Free the read/write buffer and the request */
    wr = (write_req_t *)req;
    free(wr->buf.base);
    free(wr);

    if (status == 0)
        return;

    fprintf(stderr, "uv_write error: %s - %s\n", uv_err_name(status),
            uv_strerror(status));
}

static void after_shutdown(uv_shutdown_t *req, int status) {
    ASSERT_EQ(status, 0);
    uv_close((uv_handle_t *)req->handle, on_close);
    free(req);
}

static void on_shutdown(uv_shutdown_t *req, int status) {
    ASSERT_EQ(status, 0);
    free(req);
}

static void after_read(uv_stream_t *handle, ssize_t nread,
                       const uv_buf_t *buf) {
    int i;
    write_req_t *wr;
    uv_shutdown_t *srem;
    int shutdown = 0;

    if (nread < 0) {
        /* Error or EOF */
        ASSERT_EQ(nread, UV_EOF);

        free(buf->base);
        srem = malloc(sizeof *srem);
        if (uv_is_writable(handle)) {
            ASSERT_EQ(0, uv_shutdown(srem, handle, after_shutdown));
        }
        return;
    }

    if (nread == 0) {
        /* Everything OK, but nothing read. */
        free(buf->base);
        return;
    }

    /*
     * Scan for the letter Q which signals that we should quit the server.
     * If we get QS it means close the stream.
     * If we get QSS it means shutdown the stream.
     * If we get QSH it means disable linger before close the socket.
     */
    for (i = 0; i < nread; i++) {
        if (buf->base[i] == 'Q') {
            if (i + 1 < nread && buf->base[i + 1] == 'S') {
                int reset = 0;
                if (i + 2 < nread && buf->base[i + 2] == 'S')
                    shutdown = 1;
                if (i + 2 < nread && buf->base[i + 2] == 'H')
                    reset = 1;
                if (reset && handle->type == UV_TCP)
                    ASSERT_EQ(0,
                              uv_tcp_close_reset((uv_tcp_t *)handle, on_close));
                else if (shutdown)
                    break;
                else
                    uv_close((uv_handle_t *)handle, on_close);
                free(buf->base);
                return;
            } else if (!server_closed) {
                uv_close(server, on_server_close);
                server_closed = 1;
            }
        }
    }

    wr = (write_req_t *)malloc(sizeof *wr);
    ASSERT_NOT_NULL(wr);
    wr->buf = uv_buf_init(buf->base, nread);

    if (uv_write(&wr->req, handle, &wr->buf, 1, after_write)) {
        FATAL("uv_write failed");
    }

    if (shutdown)
        ASSERT_EQ(0, uv_shutdown(malloc(sizeof *srem), handle, on_shutdown));
}

static void on_close(uv_handle_t *peer) { free(peer); }

// Each buffer is used only once and the user is responsible for
// freeing it in the uv_udp_recv_cb or the uv_read_cb callback.
static void echo_alloc(uv_handle_t *handle, size_t suggested_size,
                       uv_buf_t *buf) {
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}

static void on_connection(uv_stream_t *server, int status) {
    uv_stream_t *stream;
    int r;

    if (status != 0) {
        fprintf(stderr, "Connect error %s\n", uv_err_name(status));
    }
    ASSERT(status == 0);

    stream = malloc(sizeof(uv_tcp_t));
    ASSERT_NOT_NULL(stream);
    r = uv_tcp_init(loop, (uv_tcp_t *)stream);
    ASSERT(r == 0);

    /* associate server with stream */
    stream->data = server;

    r = uv_accept(server, stream);
    ASSERT(r == 0);

    r = uv_read_start(stream, echo_alloc, after_read);
    ASSERT(r == 0);
}

static void on_server_close(uv_handle_t *handle) { ASSERT(handle == server); }

static int tcp4_echo_start(int port) {
    struct sockaddr_in addr;
    int r;

    ASSERT(0 == uv_ip4_addr("127.0.0.1", port, &addr));

    server = (uv_handle_t *)&tcpServer;

    r = uv_tcp_init(loop, &tcpServer);
    if (r) {
        /* TODO: Error codes */
        fprintf(stderr, "Socket creation error\n");
        return 1;
    }

    r = uv_tcp_bind(&tcpServer, (const struct sockaddr *)&addr, 0);
    if (r) {
        /* TODO: Error codes */
        fprintf(stderr, "Bind error\n");
        return 1;
    }

    r = uv_listen((uv_stream_t *)&tcpServer, SOMAXCONN, on_connection);
    if (r) {
        /* TODO: Error codes */
        fprintf(stderr, "Listen error %s\n", uv_err_name(r));
        return 1;
    }

    return 0;
}

int main() {
    loop = uv_default_loop();

    if (tcp4_echo_start(8080))
        return 1;

    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
    printf("Hello world!");
}