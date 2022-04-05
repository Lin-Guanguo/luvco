# test executable
add_executable(main-test
    main-test.c
)
target_link_libraries(main-test
    PUBLIC
    luvco
)

# driver
add_executable(driver
    driver.c
)
target_link_libraries(driver
    PUBLIC
    luvco
)