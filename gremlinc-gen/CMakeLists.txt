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

# gremlinc_generate() runs the generator at build time, so a cross build
# (e.g. osx-x86_64 on an arm64 Mac) needs a second copy built for the host.
# Only when unset: under this project's policy level (CMP0126 OLD) the cache
# entry would drop a consumer's set().
if(NOT DEFINED GREMLINC_GEN_EXECUTABLE)
    set(GREMLINC_GEN_EXECUTABLE "" CACHE FILEPATH "Prebuilt host gremlinc-gen to use instead of building one")
endif()
set(_gremlinc_gen_cross OFF)
if(CMAKE_CROSSCOMPILING)
    set(_gremlinc_gen_cross ON)
elseif(APPLE AND CMAKE_OSX_ARCHITECTURES
       AND NOT ";${CMAKE_OSX_ARCHITECTURES};" MATCHES ";${CMAKE_HOST_SYSTEM_PROCESSOR};")
    set(_gremlinc_gen_cross ON)
elseif(CMAKE_GENERATOR_PLATFORM)
    # Visual Studio's -A: x64 on an AMD64 host is the only native pair it names differently.
    string(TOUPPER "${CMAKE_GENERATOR_PLATFORM}" _platform)
    string(TOUPPER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _host)
    if(NOT _platform STREQUAL _host AND NOT (_platform STREQUAL "X64" AND _host STREQUAL "AMD64"))
        set(_gremlinc_gen_cross ON)
    endif()
endif()

if(GREMLINC_GEN_EXECUTABLE)
    # A relative or bare name would resolve to the in-tree target-arch generator.
    if(NOT IS_ABSOLUTE "${GREMLINC_GEN_EXECUTABLE}" OR NOT EXISTS "${GREMLINC_GEN_EXECUTABLE}")
        message(FATAL_ERROR "GREMLINC_GEN_EXECUTABLE must be an absolute path to an existing "
                            "file, got \"${GREMLINC_GEN_EXECUTABLE}\"")
    endif()
    set_target_properties(gremlinc-gen PROPERTIES EXCLUDE_FROM_ALL ON)
    set_property(GLOBAL PROPERTY GREMLINC_GEN_COMMAND "${GREMLINC_GEN_EXECUTABLE}")
    set_property(GLOBAL PROPERTY GREMLINC_GEN_DEPENDS "${GREMLINC_GEN_EXECUTABLE}")
elseif(_gremlinc_gen_cross)
    set_target_properties(gremlinc-gen PROPERTIES EXCLUDE_FROM_ALL ON)
    include(ExternalProject)
    set(_host_dir ${CMAKE_CURRENT_BINARY_DIR}/host)
    set(_host_gen ${_host_dir}/bin/gremlinc-gen)
    if(CMAKE_HOST_WIN32 OR CMAKE_HOST_SYSTEM_NAME STREQUAL "CYGWIN")
        set(_host_gen ${_host_gen}.exe)
    endif()
    set(_host_args
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${_host_dir}/bin
        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=${_host_dir}/bin)
    if(CMAKE_HOST_APPLE)
        list(APPEND _host_args -DCMAKE_OSX_ARCHITECTURES=${CMAKE_HOST_SYSTEM_PROCESSOR})
    endif()
    # The target's compiler and SDK variables unset, and no -A or toolset, so
    # nothing of the cross toolchain reaches the host copy.
    set(_host_env ${CMAKE_COMMAND} -E env
        --unset=CC --unset=CXX --unset=CFLAGS --unset=CXXFLAGS --unset=CPPFLAGS
        --unset=LDFLAGS --unset=SDKROOT --unset=ARCHFLAGS --unset=CMAKE_TOOLCHAIN_FILE
        --unset=CMAKE_GENERATOR_PLATFORM --unset=CMAKE_GENERATOR_TOOLSET)
    ExternalProject_Add(gremlinc-gen-host
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
        BINARY_DIR ${_host_dir}
        CONFIGURE_COMMAND ${_host_env}
            ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} ${_host_args} ${CMAKE_CURRENT_SOURCE_DIR}
        BUILD_COMMAND ${_host_env}
            ${CMAKE_COMMAND} --build ${_host_dir} --target gremlinc-gen --config Release
        INSTALL_COMMAND ""
        BUILD_ALWAYS ON
        BUILD_BYPRODUCTS ${_host_gen})
    set_property(GLOBAL PROPERTY GREMLINC_GEN_COMMAND ${_host_gen})
    # The file as well as the target, so a rebuilt generator regenerates the headers.
    set_property(GLOBAL PROPERTY GREMLINC_GEN_DEPENDS gremlinc-gen-host ${_host_gen})
else()
    set_property(GLOBAL PROPERTY GREMLINC_GEN_COMMAND $<TARGET_FILE:gremlinc-gen>)
    set_property(GLOBAL PROPERTY GREMLINC_GEN_DEPENDS gremlinc-gen)
endif()

# Expose the `gremlinc_generate()` helper to anything that pulls in
# this subdirectory.  Consumers call it to hook proto files into their
# own build; see cmake/GremlincGenerate.cmake for the full contract.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/GremlincGenerate.cmake)
