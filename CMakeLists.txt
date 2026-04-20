cmake_minimum_required(VERSION 3.10)

project(gremlin LANGUAGES C)

# Top-level build: brings up every sibling project in one shot.
# Each subdirectory remains a standalone CMake project; this umbrella
# only pulls them in via add_subdirectory so `cmake --build . --target
# <anything>` works from a single build tree.
#
# Build order matters for the add_subdirectory() calls — downstream
# projects reference upstream targets (e.g. gremlinc-gen depends on
# gremlinc, gremlinc depends on gremlind + gremlinp). The guards inside
# each subproject's CMakeLists (`if(NOT TARGET <x>)`) keep a direct
# add_subdirectory from any sibling working on its own too.

enable_testing()

add_subdirectory(parser)
add_subdirectory(descriptors)
add_subdirectory(runtime)
add_subdirectory(gremlinc)
add_subdirectory(gremlinc-gen)

# Umbrella verification target — each subproject defines its own
# `verify-<name>` umbrella that only materialises when frama-c is on
# PATH. We depend on whichever ones actually exist so a missing
# frama-c leaves the top-level build clean.
set(_verify_deps)
foreach(_t IN ITEMS verify-parser verify-descriptors verify-gremlinc verify-runtime)
    if(TARGET ${_t})
        list(APPEND _verify_deps ${_t})
    endif()
endforeach()
if(_verify_deps)
    add_custom_target(verify
        DEPENDS ${_verify_deps}
        COMMENT "WP: all verification targets across the stack"
    )
endif()
