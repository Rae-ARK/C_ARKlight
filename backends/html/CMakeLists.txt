add_library(carklight_backend_html STATIC
    render.c
)

target_include_directories(carklight_backend_html
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)
