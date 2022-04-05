#include <stdio.h>
#include <stdlib.h>

#include <lua/lauxlib.h>
#include <lua/lua.h>
#include <lua/lualib.h>

#include <luvco.h>
#include <luvco/tools.h>
#include <pthread.h>

#include <stdatomic.h>

long long push_failed_count;
long long pop_failed_count;

void* push_thread(void* buf) {
    luvco_ringbuf2* r = (luvco_ringbuf2*)buf;
    for (long long i = 0; i < 10000; ++i) {
        bool failed = false;
        while (luvco_ringbuf2_push(r, (void*)i) != 0) {
            if (!failed) {
                push_failed_count++;
                failed = true;
            }
        }
    }
    printf("push done\n");
}

void* pop_thread(void* buf) {
    luvco_ringbuf2* r = (luvco_ringbuf2*)buf;
    for (long long i = 0; i < 10000; ++i) {
        long long data;
        bool failed = false;
        while (luvco_ringbuf2_pop(r, (void**)&data) != 0) {
            if (!failed) {
                pop_failed_count++;
                failed = true;
            }
        }
        assert(data == i);
    }
    printf("pop done\n");
}

void test_struct () {
    luvco_ringbuf2* r = (luvco_ringbuf2*)malloc(sizeof(luvco_ringbuf2) + sizeof(void*) * 8);
    luvco_ringbuf2_init(r, 8, 8);
    pthread_t t1;
    pthread_t t2;
    pthread_create(&t1, NULL, push_thread, (void*)r);
    pthread_create(&t2, NULL, pop_thread, (void*)r);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    luvco_ringbuf2_delete(r);
    free(r);
    printf("push failed count = %lld\n", push_failed_count);
    printf("pop failed count = %lld", pop_failed_count);
}

int main(int argc, char **argv) {
    // test_struct();
    // return 0;

    lua_State *L = luaL_newstate();
    if (argc == 2) {
        luaL_loadfile(L, argv[1]);
    } else {
        luaL_loadfile(L, "../lua/test.lua");
    }

    luvco_state* state = luvco_init(L, NULL, NULL);
    luvco_run(state);
    luvco_close(state);
    lua_close(state->main_coro);

    printf("main end");
}