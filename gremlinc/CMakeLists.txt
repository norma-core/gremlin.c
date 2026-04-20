cmake_minimum_required(VERSION 3.10)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GREMLINC_VERSION_FILE)
string(STRIP "${GREMLINC_VERSION_FILE}" GREMLINC_VERSION)
string(REGEX REPLACE "[-+].*$" "" GREMLINC_VERSION_NUMERIC "${GREMLINC_VERSION}")

project(gremlinc VERSION ${GREMLINC_VERSION_NUMERIC} LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PARSER_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/../parser)
set(DESCRIPTORS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../descriptors)
set(RUNTIME_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/../runtime)

# Parser + descriptors sources (linked in directly — single build unit so
# gremlinc can be used without separately built static libs, same pattern
# descriptors uses for parser).
file(GLOB PARSER_SOURCES ${PARSER_DIR}/src/entries/*.c ${PARSER_DIR}/src/lib.c)
file(GLOB DESCRIPTORS_SOURCES ${DESCRIPTORS_DIR}/src/*.c)

set(GREMLINC_SOURCES
    src/writer.c
    src/naming.c
    src/const_convert.c
    src/emit_enum.c
    src/emit_common.c
    src/emit_scalar.c
    src/emit_bytes.c
    src/emit_enum_ref.c
    src/emit_msg_ref.c
    src/emit_repeated_bytes.c
    src/emit_repeated_msg.c
    src/emit_repeated_scalar.c
    src/emit_map.c
    src/emit_message.c
    src/lib.c
)

add_library(gremlinc STATIC
    ${PARSER_SOURCES}
    ${DESCRIPTORS_SOURCES}
    ${GREMLINC_SOURCES}
)

target_include_directories(gremlinc PUBLIC
    $<BUILD_INTERFACE:${PARSER_DIR}/include>
    $<BUILD_INTERFACE:${DESCRIPTORS_DIR}/include>
    $<BUILD_INTERFACE:${RUNTIME_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

enable_testing()

set(TEST_SOURCES
    tests/all_tests.c
    tests/naming_test.c
    tests/const_convert_test.c
    tests/enum_test.c
)

add_executable(gremlinc_tests ${TEST_SOURCES})
target_link_libraries(gremlinc_tests gremlinc)
target_include_directories(gremlinc_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/tests
)

add_test(NAME gremlinc_tests COMMAND gremlinc_tests)

# -------------------------------------------------------------------------
# FRAMA-C formal verification of gremlinc internals.
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
        -wp -wp-rte -wp-timeout 120
        -wp-prover alt-ergo,z3
        -wp-par ${NPROC}
        -wp-cache update
        -wp-cache-dir ${WP_CACHE_DIR}
        -wp-smoke-tests
        -wp-msg-key shell
        -kernel-warn-key annot:missing-spec=inactive
        -wp-warn-key pedantic-assigns=inactive
        -wp-no-warn-memory-model
        -cpp-extra-args=-I${RUNTIME_DIR}/include\ -I${CMAKE_CURRENT_SOURCE_DIR}/include\ -I${CMAKE_CURRENT_SOURCE_DIR}/src\ -I${DESCRIPTORS_DIR}/include\ -I${PARSER_DIR}/include
    )

    add_custom_target(verify-snake-case
        COMMAND ${FRAMAC} ${WP_BASE_FLAGS}
                -wp-fct gremlinc_to_snake_case,is_upper_c,is_lower_c,is_lower_or_digit_c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/naming.c
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "WP: camelCase → snake_case converter"
        VERBATIM
    )

    # -warn-special-float=none disables the RTE check that flags
    # int64→float casts as potentially producing inf/NaN. Proto allows
    # `[default = inf]` / `[default = nan]` explicitly (the parser
    # resolves those identifiers to IEEE-754 values), so a float result
    # that IS inf/NaN is a valid output, not a bug.
    add_custom_target(verify-const-convert
        COMMAND ${FRAMAC} ${WP_BASE_FLAGS}
                -warn-special-float none
                -wp-fct gremlinc_const_to_int32,gremlinc_const_to_int64,gremlinc_const_to_uint32,gremlinc_const_to_uint64,gremlinc_const_to_float,gremlinc_const_to_double
                ${CMAKE_CURRENT_SOURCE_DIR}/src/const_convert.c
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "WP: proto-const → typed-C numeric conversions"
        VERBATIM
    )

    add_custom_target(verify-gremlinc
        DEPENDS verify-snake-case verify-const-convert
        COMMENT "WP: all gremlinc verification targets"
    )
endif()
