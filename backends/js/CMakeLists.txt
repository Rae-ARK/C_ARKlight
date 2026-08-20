add_library(carklight_backend_js STATIC
    render.c
)

target_include_directories(carklight_backend_js
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)
