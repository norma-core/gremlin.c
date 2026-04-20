cmake_minimum_required(VERSION 3.10)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GREMLIND_VERSION_FILE)
string(STRIP "${GREMLIND_VERSION_FILE}" GREMLIND_VERSION)
string(REGEX REPLACE "[-+].*$" "" GREMLIND_VERSION_NUMERIC "${GREMLIND_VERSION}")

project(gremlind VERSION ${GREMLIND_VERSION_NUMERIC} LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The parser is linked as sources (not a separate static lib) so the whole
# stack can be verified or instrumented in one pass.
set(PARSER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../parser)

set(PARSER_SOURCES
    ${PARSER_DIR}/src/entries/buffer.c
    ${PARSER_DIR}/src/entries/edition.c
    ${PARSER_DIR}/src/entries/enum.c
    ${PARSER_DIR}/src/entries/errors.c
    ${PARSER_DIR}/src/entries/extend.c
    ${PARSER_DIR}/src/entries/extensions.c
    ${PARSER_DIR}/src/entries/field.c
    ${PARSER_DIR}/src/entries/field_type.c
    ${PARSER_DIR}/src/entries/group.c
    ${PARSER_DIR}/src/entries/import.c
    ${PARSER_DIR}/src/entries/lexems.c
    ${PARSER_DIR}/src/entries/message.c
    ${PARSER_DIR}/src/entries/oneof.c
    ${PARSER_DIR}/src/entries/option.c
    ${PARSER_DIR}/src/entries/options.c
    ${PARSER_DIR}/src/entries/package.c
    ${PARSER_DIR}/src/entries/std.c
    ${PARSER_DIR}/src/entries/proto_file.c
    ${PARSER_DIR}/src/entries/reserved.c
    ${PARSER_DIR}/src/entries/service.c
    ${PARSER_DIR}/src/entries/syntax.c
    ${PARSER_DIR}/src/lib.c
)

set(AST_SOURCES
    src/arena.c
    src/arena_std.c
    src/build.c
    src/extend.c
    src/name.c
    src/resolve.c
    src/lib.c
)

# Sources the verifier sees. arena_std.c is intentionally excluded — same
# pattern as parser/src/entries/std.c.
set(AST_VERIFIED_SOURCES
    src/arena.c
)

add_library(gremlind STATIC ${PARSER_SOURCES} ${AST_SOURCES})

target_include_directories(gremlind PUBLIC
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${PARSER_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

enable_testing()

set(TEST_SOURCES
    tests/all_tests.c
    tests/arena_test.c
    tests/build_test.c
    tests/name_test.c
    tests/resolve_test.c
    tests/integration_test.c
)

add_executable(descriptors_tests ${TEST_SOURCES})
target_link_libraries(descriptors_tests gremlind)
target_include_directories(descriptors_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/tests
)
get_filename_component(TEST_DATA_DIR tests ABSOLUTE)
target_compile_definitions(descriptors_tests PRIVATE "TEST_DATA_DIR=\"${TEST_DATA_DIR}\"")
add_test(NAME descriptors_tests COMMAND descriptors_tests)

# -------------------------------------------------------------------------
# FRAMA-C formal verification (requires frama-c on PATH)
#
# The parser is already verified in its own tree; here we only run WP over
# the gremlind sources that matter. Parser sources are NOT included in the
# verify set, to keep proof time bounded and avoid re-proving what the
# parser's own `make verify` covers.
#
# Smoke tests are mandatory from day one — axiom-poisoning would otherwise
# silently turn proofs vacuous.
# -------------------------------------------------------------------------
find_program(FRAMAC frama-c)
if(FRAMAC)
    include(ProcessorCount)
    ProcessorCount(NPROC)
    if(NPROC EQUAL 0)
        set(NPROC 4)
    endif()

    set(WP_CACHE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.wp-cache)
    set(WP_BASE_FLAGS
        -wp -wp-rte -wp-timeout 60
        -wp-prover z3,alt-ergo
        -wp-par ${NPROC}
        -wp-cache update
        -wp-cache-dir ${WP_CACHE_DIR}
        -wp-smoke-tests
        -wp-msg-key shell
        -kernel-warn-key annot:missing-spec=inactive
        -wp-warn-key pedantic-assigns=inactive
        -wp-no-warn-memory-model
        -wp-warn-key union=inactive
        -cpp-extra-args=-I${CMAKE_CURRENT_SOURCE_DIR}/include\ -I${PARSER_DIR}/include
    )

    # gremlind_arena_alloc is intentionally excluded from WP: it calls an
    # opaque function pointer (the grow callback), which WP can only reason
    # about with additional callee contracts. The verified core is the
    # primitives — init, push_chunk, try_alloc, bytes_used — and the
    # auto-grow path is a thin unverified wrapper over them.
    set(ARENA_WP_FCTS
        gremlind_arena_init,gremlind_arena_push_chunk,gremlind_arena_try_alloc,gremlind_arena_bytes_used
    )

    add_custom_target(verify-arena
        COMMAND ${FRAMAC} ${WP_BASE_FLAGS} -wp-fct ${ARENA_WP_FCTS} ${AST_VERIFIED_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "WP: arena"
        VERBATIM
    )

    add_custom_target(verify-descriptors
        DEPENDS verify-arena
        COMMENT "WP: all gremlind verification targets"
    )
endif()
