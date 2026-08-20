cmake_minimum_required(VERSION 3.16)
project(carklight C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# docs/IMPLEMENTATION.md Stage 0's "test" is: build a tree by hand,
# free it, run under a memory checker, confirm zero leaks and zero
# double-frees. This option wires that in as ordinary sanitizer flags
# rather than requiring a separately installed tool (valgrind), so the
# check runs anywhere this project builds.
option(CARKLIGHT_ENABLE_ASAN "Build with AddressSanitizer + LeakSanitizer" OFF)
if(CARKLIGHT_ENABLE_ASAN)
    add_compile_options(-fsanitize=address,leak -g -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,leak)
endif()

add_compile_options(-Wall -Wextra)

# docs/ADDENDUM.md §4: modular internally, one libcarklight externally.
# core/ is the only piece Stage 0 builds; each backend
# (backends/html, backends/css, backends/js, ...) adds itself to
# CARKLIGHT_BACKEND_LIBS as its own stage lands (Stage 4 onward) and
# is linked into the one shared object below — carklight_core itself
# never changes shape to accommodate that.
add_subdirectory(core)
set_target_properties(carklight_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(CARKLIGHT_BACKEND_LIBS "")

# Stage 4 (docs/IMPLEMENTATION.md): first backend directory to land.
# Each backend below adds itself to CARKLIGHT_BACKEND_LIBS and to the
# PIC property list the same way — core/'s own add_subdirectory above
# never changes to accommodate a new one.
add_subdirectory(backends/html)
set_target_properties(carklight_backend_html PROPERTIES POSITION_INDEPENDENT_CODE ON)
list(APPEND CARKLIGHT_BACKEND_LIBS carklight_backend_html)

# Stage 5a (docs/IMPLEMENTATION.md): CSS backend. Same wiring pattern
# as HTML above — this block is the only thing that changes to add a
# backend; core/ and carklight_backend_html are untouched.
add_subdirectory(backends/css)
set_target_properties(carklight_backend_css PROPERTIES POSITION_INDEPENDENT_CODE ON)
list(APPEND CARKLIGHT_BACKEND_LIBS carklight_backend_css)

add_library(carklight SHARED lib_glue.c)
target_link_libraries(carklight PRIVATE carklight_core ${CARKLIGHT_BACKEND_LIBS})
target_include_directories(carklight PUBLIC ${CMAKE_SOURCE_DIR}/include)
set_target_properties(carklight PROPERTIES PUBLIC_HEADER include/carklight.h)

enable_testing()
add_subdirectory(tests)
