add_executable(bench-driver
    bench-driver.c
)

target_link_libraries(bench-driver
    PUBLIC
    luvco
)

