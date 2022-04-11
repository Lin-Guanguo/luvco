cmake_minimum_required(VERSION 3.16)
project("luvco")

find_package(libuv)

# luvco library
add_library(luvco
    STATIC
    ./src/tools.c
    ./src/log.c
    ./src/luvco.c
    ./src/net.c
    ./src/ringbuf.c
    ./src/object.c
    ./src/chan
    ./src/scheduler.c
)
target_include_directories(luvco
    PUBLIC
    ./include
)
target_link_libraries(luvco
    PUBLIC
    uv
    lua
    m
)

add_subdirectory(test)
add_subdirectory(unittest)
add_subdirectory(bench)
