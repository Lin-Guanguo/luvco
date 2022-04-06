
find_package(unity)

add_executable(test
    test.c
    test-ringbuf.c
    test-spinlock.c
)
target_link_libraries(test
    PUBLIC
    luvco
    unity::framework
)