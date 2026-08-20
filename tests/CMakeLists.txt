add_executable(test_stage0_alloc test_stage0_alloc.c)
target_link_libraries(test_stage0_alloc PRIVATE carklight_core)

add_test(NAME stage0_alloc COMMAND test_stage0_alloc)

add_executable(test_normalize test_normalize.c)
target_link_libraries(test_normalize PRIVATE carklight_core)

add_test(NAME stage1_normalize COMMAND test_normalize)

add_executable(test_validate test_validate.c)
target_link_libraries(test_validate PRIVATE carklight_core)

add_test(NAME stage2_validate COMMAND test_validate)
