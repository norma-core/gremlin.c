cmake_minimum_required(VERSION 3.10)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GREMLIN_VERSION_FILE)
string(STRIP "${GREMLIN_VERSION_FILE}" GREMLIN_VERSION)
string(REGEX REPLACE "[-+].*$" "" GREMLIN_VERSION_NUMERIC "${GREMLIN_VERSION}")

project(gremlin VERSION ${GREMLIN_VERSION_NUMERIC} LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Header-only — just an INTERFACE target that exposes the include dir.
# No library artefact, no runtime tests: every function's behaviour is
# specified and proven via ACSL + Frama-C WP, so repeating the same
# checks at runtime would duplicate the proofs.
add_library(gremlin_runtime INTERFACE)
target_include_directories(gremlin_runtime INTERFACE
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Runtime sanity for things WP can't verify — specifically, the union
# type-punning in gremlin_f32/f64_* maps to IEEE-754 bits. ACSL proves
# memory safety; this test proves the bit pattern.
add_executable(float_test tests/float_test.c)
target_link_libraries(float_test gremlin_runtime)
target_link_libraries(float_test m)
target_include_directories(float_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/tests
)
enable_testing()
add_test(NAME float_test COMMAND float_test)

# -------------------------------------------------------------------------
# FRAMA-C formal verification (requires frama-c on PATH).
# Verifies the static-inline definitions via tests/verify.c — that TU
# #includes gremlin.h and references every runtime function so WP sees
# the bodies.
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
        -cpp-extra-args=-I${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    set(RUNTIME_WP_FCTS gremlin_varint_size,gremlin_varint_size_cascade,gremlin_varint_size_clz,gremlin_varint_decode,gremlin_writer_init,gremlin_varint_encode,gremlin_varint32_size,gremlin_varint32_encode,gremlin_varint32_decode,gremlin_fixed32_size,gremlin_fixed32_encode,gremlin_fixed32_decode,gremlin_fixed64_size,gremlin_fixed64_encode,gremlin_fixed64_decode,gremlin_skip_data,gremlin_write_bytes,gremlin_bytes_decode,gremlin_zigzag32,gremlin_unzigzag32,gremlin_zigzag64,gremlin_unzigzag64,gremlin_f32_bits,gremlin_f32_from_bits,gremlin_f64_bits,gremlin_f64_from_bits,gremlin_varint_encode_at,gremlin_varint32_encode_at,gremlin_fixed32_encode_at,gremlin_fixed64_encode_at,gremlin_write_bytes_at)

    add_custom_target(verify-runtime
        COMMAND ${FRAMAC} ${WP_BASE_FLAGS} -wp-fct ${RUNTIME_WP_FCTS} tests/verify.c
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "WP: runtime"
        VERBATIM
    )
endif()
