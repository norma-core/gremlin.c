# GremlincGenerate.cmake
#
# Adds the `gremlinc_generate()` function — the CMake integration for
# gremlinc-gen. Consumers do:
#
#     add_subdirectory(path/to/gremlin.c)  # brings in gremlinc-gen + this module
#     gremlinc_generate(
#         TARGET       my_protos
#         IMPORTS_ROOT ${CMAKE_SOURCE_DIR}/protos
#         PROTOS       foo.proto nested/bar.proto
#     )
#     target_link_libraries(my_exe PRIVATE my_protos)
#
# The call creates:
#
#   - One `add_custom_command` per invocation that runs the
#     `gremlinc-gen` binary, with the generated `.pb.h` files declared
#     as OUTPUTs so CMake's build graph handles incremental rebuilds.
#
#   - A concrete target `<TARGET>` (INTERFACE library) that carries
#     the generated headers' include directory on its interface plus
#     a transitive link to `gremlin_runtime` for `#include "gremlin.h"`.
#     `target_link_libraries(<consumer> PRIVATE <TARGET>)` is all a
#     downstream target needs.
#
# Arguments:
#
#   TARGET <name>            — required. Name of the INTERFACE library
#                              target to create.
#   IMPORTS_ROOT <dir>       — required. Passed as `-R` to gremlinc-gen.
#                              All PROTOS paths are relative to this.
#   OUT_DIR <dir>            — optional. Where the .pb.h files go.
#                              Default: ${CMAKE_CURRENT_BINARY_DIR}/<TARGET>.
#   PROTOS <files>...        — required. Paths relative to IMPORTS_ROOT.
#                              Every file that participates in the
#                              compile must be listed here; imports
#                              between them resolve via the shared
#                              name scope.

function(gremlinc_generate)
    set(_options)
    set(_one_value TARGET IMPORTS_ROOT OUT_DIR)
    set(_multi_value PROTOS)
    cmake_parse_arguments(GG "${_options}" "${_one_value}" "${_multi_value}" ${ARGN})

    if(NOT GG_TARGET)
        message(FATAL_ERROR "gremlinc_generate: TARGET is required")
    endif()
    if(NOT GG_IMPORTS_ROOT)
        message(FATAL_ERROR "gremlinc_generate: IMPORTS_ROOT is required")
    endif()
    if(NOT GG_PROTOS)
        message(FATAL_ERROR "gremlinc_generate: PROTOS is required (at least one)")
    endif()
    if(NOT GG_OUT_DIR)
        set(GG_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${GG_TARGET}")
    endif()
    if(NOT TARGET gremlinc-gen)
        message(FATAL_ERROR
            "gremlinc_generate: target `gremlinc-gen` not found. "
            "Make sure the gremlinc-gen subproject has been included "
            "via add_subdirectory before calling this function.")
    endif()

    # Build parallel lists of:
    #   _generated:     absolute paths of the .pb.h outputs (for OUTPUT/DEPENDS)
    #   _proto_abs:     absolute paths of the .proto inputs (for DEPENDS)
    set(_generated)
    set(_proto_abs)
    foreach(_proto IN LISTS GG_PROTOS)
        string(REGEX REPLACE "\\.proto$" ".pb.h" _rel "${_proto}")
        list(APPEND _generated "${GG_OUT_DIR}/${_rel}")
        list(APPEND _proto_abs "${GG_IMPORTS_ROOT}/${_proto}")
    endforeach()

    add_custom_command(
        OUTPUT  ${_generated}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GG_OUT_DIR}
        COMMAND $<TARGET_FILE:gremlinc-gen>
                -R ${GG_IMPORTS_ROOT}
                -o ${GG_OUT_DIR}
                ${GG_PROTOS}
        DEPENDS gremlinc-gen ${_proto_abs}
        COMMENT "gremlinc-gen: generating ${GG_TARGET} headers into ${GG_OUT_DIR}"
        VERBATIM
    )

    # Custom target pinned to the OUTPUTs so `make <TARGET>_generate`
    # works as a standalone regenerate command. Interface library
    # `add_dependencies` calls only work for targets that produce
    # build output (custom targets do, INTERFACE libs don't — hence
    # the two-target split).
    add_custom_target(${GG_TARGET}_generate DEPENDS ${_generated})

    add_library(${GG_TARGET} INTERFACE)
    add_dependencies(${GG_TARGET} ${GG_TARGET}_generate)
    target_include_directories(${GG_TARGET} INTERFACE ${GG_OUT_DIR})
    target_link_libraries(${GG_TARGET} INTERFACE gremlin_runtime)
endfunction()
