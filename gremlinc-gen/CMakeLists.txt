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

add_executable(gremlinc-gen
    src/main.c
    src/args.c
    src/io.c
    src/sources.c
    src/emit_file.c
)
target_link_libraries(gremlinc-gen PRIVATE gremlinc)
