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

# test executable
add_executable(main-test
    test/main-test.c
)
target_link_libraries(main-test
    PUBLIC
    luvco
)

# test libuv
add_executable(libuv-echo
    test/libuv-echo.c
)
target_link_libraries(libuv-echo
    PUBLIC
    luvco
)

# driver
add_executable(driver
    test/driver.c
)
target_link_libraries(driver
    PUBLIC
    luvco
)
