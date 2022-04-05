
find_package(unity)

add_executable(test
    test.c
    test-ringbuf.c
)
target_link_libraries(test
    PUBLIC
    luvco
    unity::framework
)