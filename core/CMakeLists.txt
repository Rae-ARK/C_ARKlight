add_library(carklight_core STATIC
    node.c
    site.c
    build_result.c
    normalize.c
    validate.c
    ir_build.c
)

target_include_directories(carklight_core
    PUBLIC ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
