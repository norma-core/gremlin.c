cmake_minimum_required(VERSION 3.10)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GREMLINP_VERSION_FILE)
string(STRIP "${GREMLINP_VERSION_FILE}" GREMLINP_VERSION)

project(gremlinp VERSION ${GREMLINP_VERSION} LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source files
set(PARSER_SOURCES
    src/entries/buffer.c
    src/entries/edition.c
    src/entries/enum.c
    src/entries/errors.c
    src/entries/extend.c
    src/entries/extensions.c
    src/entries/field.c
    src/entries/field_type.c
    src/entries/group.c
    src/entries/import.c
    src/entries/lexems.c
    src/entries/message.c
    src/entries/oneof.c
    src/entries/option.c
    src/entries/options.c
    src/entries/package.c
    src/entries/proto_file.c
    src/entries/reserved.c
    src/entries/service.c
    src/entries/syntax.c
    src/lib.c
)

# Create static library
add_library(gremlinp STATIC ${PARSER_SOURCES})

# Include directories
target_include_directories(gremlinp PUBLIC
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Tests
enable_testing()

set(TEST_SOURCES
    tests/all_tests.c
    tests/lexems_test.c
    tests/integration_test.c
)

add_executable(parser_tests ${TEST_SOURCES})
target_link_libraries(parser_tests gremlinp)
get_filename_component(TEST_DATA_DIR ../test_data ABSOLUTE)
target_compile_definitions(parser_tests PRIVATE "TEST_DATA_DIR=\"${TEST_DATA_DIR}\"")
add_test(NAME parser_tests COMMAND parser_tests)

# FRAMA-C formal verification (requires frama-c on PATH)
find_program(FRAMAC frama-c)
if(FRAMAC)
    include(ProcessorCount)
    ProcessorCount(NPROC)
    if(NPROC EQUAL 0)
        set(NPROC 4)
    endif()

    set(WP_CACHE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.wp-cache)
    set(WP_BASE_FLAGS
        -wp -wp-rte -wp-timeout 30
        -wp-prover z3,alt-ergo
        -wp-par ${NPROC}
        -wp-cache update
        -wp-cache-dir ${WP_CACHE_DIR}
        -kernel-warn-key annot:missing-spec=inactive
        -wp-warn-key pedantic-assigns=inactive
        -wp-no-warn-memory-model
        -wp-warn-key union=inactive
        -cpp-extra-args=-I${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    set(BUFFER_SRC src/entries/buffer.c)
    set(LEXEMS_SRC src/entries/lexems.c ${BUFFER_SRC})
    set(SYNTAX_SRC
        src/entries/syntax.c src/entries/edition.c src/entries/package.c
        src/entries/option.c src/entries/options.c src/entries/import.c
        src/entries/extensions.c src/entries/reserved.c src/entries/service.c
        src/entries/group.c src/entries/enum.c src/entries/field_type.c
        src/entries/field.c src/entries/oneof.c src/entries/extend.c
        src/entries/message.c src/entries/proto_file.c src/entries/errors.c
        ${LEXEMS_SRC}
    )

    add_custom_target(verify-buffer
        COMMAND ${FRAMAC} ${WP_BASE_FLAGS} ${BUFFER_SRC}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "WP: buffer"
        VERBATIM
    )

    add_custom_target(verify-lexems
        COMMAND ${FRAMAC} ${WP_BASE_FLAGS} ${LEXEMS_SRC}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "WP: lexems"
        VERBATIM
    )

    add_custom_target(verify-syntax
        COMMAND ${FRAMAC} ${WP_BASE_FLAGS} ${SYNTAX_SRC}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "WP: syntax"
        VERBATIM
    )

    add_custom_target(verify
        DEPENDS verify-buffer verify-lexems verify-syntax
        COMMENT "All FRAMA-C WP verifications"
    )
endif()
