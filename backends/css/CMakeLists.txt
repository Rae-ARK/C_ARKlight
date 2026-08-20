add_library(carklight_backend_css STATIC
    render.c
)

target_include_directories(carklight_backend_css
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)
