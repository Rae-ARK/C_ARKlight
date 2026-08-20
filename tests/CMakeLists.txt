add_executable(test_stage0_alloc test_stage0_alloc.c)
target_link_libraries(test_stage0_alloc PRIVATE carklight_core)

add_test(NAME stage0_alloc COMMAND test_stage0_alloc)

add_executable(test_normalize test_normalize.c)
target_link_libraries(test_normalize PRIVATE carklight_core)

add_test(NAME stage1_normalize COMMAND test_normalize)

add_executable(test_validate test_validate.c)
target_link_libraries(test_validate PRIVATE carklight_core)

add_test(NAME stage2_validate COMMAND test_validate)

add_executable(test_ir_build test_ir_build.c)
target_link_libraries(test_ir_build PRIVATE carklight_core)

add_test(NAME stage3_ir_build COMMAND test_ir_build)

add_executable(test_html_backend test_html_backend.c)
target_link_libraries(test_html_backend PRIVATE carklight_backend_html carklight_core)

add_test(NAME stage4_html_backend COMMAND test_html_backend)

add_executable(test_css_backend test_css_backend.c)
target_link_libraries(test_css_backend PRIVATE carklight_backend_css carklight_core)

add_test(NAME stage5a_css_backend COMMAND test_css_backend)

add_executable(test_js_backend test_js_backend.c)
target_link_libraries(test_js_backend PRIVATE carklight_backend_js carklight_core)

add_test(NAME stage5b_js_backend COMMAND test_js_backend)
