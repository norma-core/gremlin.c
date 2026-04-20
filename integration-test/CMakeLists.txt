cmake_minimum_required(VERSION 3.10)

project(gremlin_integration_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(REPO_ROOT   ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CORPUS_DIR  ${REPO_ROOT}/descriptors/tests/proto_corpus)

# Pull in gremlinc-gen so we have the generator binary + the
# `gremlinc_generate()` helper available.
if(NOT TARGET gremlinc-gen)
    add_subdirectory(${REPO_ROOT}/gremlinc-gen
                     ${CMAKE_CURRENT_BINARY_DIR}/gremlinc-gen-build)
endif()

# Generate the .pb.h set this test depends on.
# unittest.proto transitively pulls in unittest_import / unittest_import_public
# via imports — every file that participates must be listed here so
# gremlinc-gen has them all in one compile batch.
gremlinc_generate(
    TARGET       integration_protos
    IMPORTS_ROOT ${CORPUS_DIR}
    PROTOS       unittest.proto
                 unittest_import.proto
                 unittest_import_public.proto
                 map_test.proto
                 whatsapp.proto
                 benchmark.proto
)

add_executable(integration_test main.c)
target_link_libraries(integration_test PRIVATE integration_protos)
target_include_directories(integration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(integration_test PRIVATE
    "INTEGRATION_TEST_BINARIES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/binaries\""
)
target_compile_options(integration_test PRIVATE
    -Wno-unused-function -Wno-gnu-zero-variadic-macro-arguments
)

# Release-optimised benchmark binary — gremlin.zig bench runs with
# `-Doptimize=ReleaseFast` so anything less is an apples-to-oranges
# comparison. Kept separate from integration_test so the test binary
# can stay debug-friendly.
add_executable(benchmark benchmark.c)
target_link_libraries(benchmark PRIVATE integration_protos)
target_include_directories(benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(benchmark PRIVATE
    "INTEGRATION_TEST_BINARIES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/binaries\""
)
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(benchmark PRIVATE
        -O3 -DNDEBUG -march=native -flto=thin
        -Wno-unused-function -Wno-gnu-zero-variadic-macro-arguments
    )
    target_link_options(benchmark PRIVATE -flto=thin)
else()
    target_compile_options(benchmark PRIVATE
        -O3 -DNDEBUG -march=native -flto -Wno-unused-function
    )
    target_link_options(benchmark PRIVATE -flto)
endif()

enable_testing()
add_test(NAME integration_test COMMAND integration_test)
