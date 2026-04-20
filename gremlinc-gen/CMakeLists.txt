cmake_minimum_required(VERSION 3.10)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GREMLINC_GEN_VERSION_FILE)
string(STRIP "${GREMLINC_GEN_VERSION_FILE}" GREMLINC_GEN_VERSION)
string(REGEX REPLACE "[-+].*$" "" GREMLINC_GEN_VERSION_NUMERIC "${GREMLINC_GEN_VERSION}")

project(gremlinc_gen VERSION ${GREMLINC_GEN_VERSION_NUMERIC} LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Pull in gremlinc — it already statically links parser + descriptors, so
# linking against it is all we need.
if(NOT TARGET gremlinc)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../gremlinc
                     ${CMAKE_CURRENT_BINARY_DIR}/gremlinc-build)
endif()

# The generated .pb.h files `#include "gremlin.h"` from the runtime, so
# consumers of `gremlinc_generate()` also need `gremlin_runtime`
# visible. Pull it in if we haven't already.
if(NOT TARGET gremlin_runtime)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../runtime
                     ${CMAKE_CURRENT_BINARY_DIR}/runtime-build)
endif()

add_executable(gremlinc-gen
    src/main.c
    src/args.c
    src/io.c
    src/sources.c
    src/emit_file.c
)
target_link_libraries(gremlinc-gen PRIVATE gremlinc)

# Expose the `gremlinc_generate()` helper to anything that pulls in
# this subdirectory.  Consumers call it to hook proto files into their
# own build; see cmake/GremlincGenerate.cmake for the full contract.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/GremlincGenerate.cmake)
