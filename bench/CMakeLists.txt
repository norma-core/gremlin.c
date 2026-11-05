cmake_minimum_required(VERSION 3.24)

# gremlin.c against Google's C++ protobuf, upb, protobuf-c, nanopb and prost on
# the same bytes. The other libraries come from the system, see README.md here.
project(gremlin_bench C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO_ROOT  ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CORPUS_DIR ${REPO_ROOT}/descriptors/tests/proto_corpus)
set(GEN        ${CMAKE_CURRENT_BINARY_DIR}/gen)
set(PROTOS benchmark.proto unittest.proto unittest_import.proto unittest_import_public.proto)
set(PROTO_FILES)
foreach(_p ${PROTOS})
    list(APPEND PROTO_FILES ${CORPUS_DIR}/${_p})
endforeach()

# The gremlin side is the existing benchmark target.
add_subdirectory(${REPO_ROOT}/integration-test ${CMAKE_CURRENT_BINARY_DIR}/gremlin)

# Each other library is built when it is found; run.sh marks a missing one n/a.
find_package(protobuf CONFIG QUIET)
if(NOT protobuf_FOUND)
    # Distro packages without the CMake config, e.g. Ubuntu's protobuf 3.21.
    find_package(Protobuf QUIET)
endif()
# The protoc of the protobuf found, so generated code matches its library.
if(TARGET protobuf::protoc)
    set(PROTOC $<TARGET_FILE:protobuf::protoc>)
elseif(Protobuf_PROTOC_EXECUTABLE)
    set(PROTOC ${Protobuf_PROTOC_EXECUTABLE})
else()
    find_program(PROTOC protoc REQUIRED)
endif()
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PROTOBUF_C QUIET IMPORTED_TARGET libprotobuf-c)
endif()
find_program(PROTOC_GEN_C protoc-gen-c)

# The same optimisation as the gremlin benchmark target, LTO included, chosen
# by the same test (AppleClang gets full LTO there too).
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(_lto -flto=thin)
else()
    set(_lto -flto)
endif()
set(_flags -O3 -DNDEBUG -march=native ${_lto})
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    list(APPEND _flags -Wno-gnu-zero-variadic-macro-arguments)
endif()

# Shared input: the DeepNested message the gremlin benchmark builds, as text.
add_custom_command(
    OUTPUT ${GEN}/deep_nested.bin
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN}
    COMMAND ${PROTOC} -I ${CORPUS_DIR} --encode=benchmark.DeepNested benchmark.proto
            < ${CMAKE_CURRENT_SOURCE_DIR}/deep_nested.txtpb > ${GEN}/deep_nested.bin
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/deep_nested.txtpb ${CORPUS_DIR}/benchmark.proto
    VERBATIM)
add_custom_target(bench_inputs ALL DEPENDS ${GEN}/deep_nested.bin)

set(_benches benchmark bench_inputs)
set(_cpp_out)
set(_upb_out)
set(_pbc_out)
foreach(_p ${PROTOS})
    string(REGEX REPLACE "\\.proto$" "" _b ${_p})
    list(APPEND _cpp_out ${GEN}/cpp/${_b}.pb.cc)
    list(APPEND _upb_out ${GEN}/upb/${_b}.upb_minitable.c)
    list(APPEND _pbc_out ${GEN}/pbc/${_b}.pb-c.c)
endforeach()

if(TARGET protobuf::libprotobuf)
    # C++ rejects ctype=CORD on an extension, so its copy of unittest.proto drops it there.
    set(_cpp_src ${GEN}/cpp_src)
    file(MAKE_DIRECTORY ${_cpp_src} ${GEN}/cpp)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CORPUS_DIR}/unittest.proto)
    file(READ ${CORPUS_DIR}/unittest.proto _unittest)
    string(REGEX REPLACE "(_cord_extension = [0-9]+ \\[)ctype=CORD,? ?" "\\1" _unittest "${_unittest}")
    string(REGEX REPLACE "(_cord_extension = [0-9]+) \\[\\]" "\\1" _unittest "${_unittest}")
    file(WRITE ${_cpp_src}/unittest.proto "${_unittest}")
    foreach(_p benchmark.proto unittest_import.proto unittest_import_public.proto)
        configure_file(${CORPUS_DIR}/${_p} ${_cpp_src}/${_p} COPYONLY)
    endforeach()
    add_custom_command(
        OUTPUT ${_cpp_out}
        COMMAND ${PROTOC} -I ${_cpp_src} --cpp_out=${GEN}/cpp ${PROTOS}
        DEPENDS ${_cpp_src}/unittest.proto ${_cpp_src}/benchmark.proto
                ${_cpp_src}/unittest_import.proto ${_cpp_src}/unittest_import_public.proto
        VERBATIM)
    add_executable(bench_cpp bench_cpp.cc ${_cpp_out})
    target_include_directories(bench_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GEN}/cpp)
    target_link_libraries(bench_cpp PRIVATE protobuf::libprotobuf)
    target_compile_options(bench_cpp PRIVATE ${_flags})
    list(APPEND _benches bench_cpp)
else()
    message(STATUS "protobuf C++ not found, skipped")
endif()

if(TARGET protobuf::libupb AND TARGET protobuf::protoc-gen-upb AND
   TARGET protobuf::protoc-gen-upb_minitable)
    file(MAKE_DIRECTORY ${GEN}/upb)
    add_custom_command(
        OUTPUT ${_upb_out}
        COMMAND ${PROTOC} -I ${CORPUS_DIR}
                --plugin=protoc-gen-upb=$<TARGET_FILE:protobuf::protoc-gen-upb>
                --plugin=protoc-gen-upb_minitable=$<TARGET_FILE:protobuf::protoc-gen-upb_minitable>
                --upb_out=${GEN}/upb --upb_minitable_out=${GEN}/upb ${PROTOS}
        DEPENDS ${PROTO_FILES} protobuf::protoc-gen-upb protobuf::protoc-gen-upb_minitable
        VERBATIM)
    add_executable(bench_upb bench_upb.c ${_upb_out})
    target_include_directories(bench_upb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GEN}/upb)
    target_link_libraries(bench_upb PRIVATE protobuf::libupb)
    target_compile_options(bench_upb PRIVATE ${_flags})
    list(APPEND _benches bench_upb)
else()
    message(STATUS "upb not found, skipped")
endif()

if(PROTOBUF_C_FOUND AND PROTOC_GEN_C)
    file(MAKE_DIRECTORY ${GEN}/pbc)
    add_custom_command(
        OUTPUT ${_pbc_out}
        COMMAND ${PROTOC} -I ${CORPUS_DIR} --plugin=protoc-gen-c=${PROTOC_GEN_C}
                --c_out=${GEN}/pbc ${PROTOS}
        DEPENDS ${PROTO_FILES} ${PROTOC_GEN_C}
        VERBATIM)
    # protoc-gen-c writes the extreme float defaults as bare inf and nan.
    set_source_files_properties(${GEN}/pbc/unittest.pb-c.c PROPERTIES
        COMPILE_DEFINITIONS "inf=__builtin_inf();nan=__builtin_nan(\"\")"
        COMPILE_OPTIONS -w)
    add_executable(bench_protobuf_c bench_protobuf_c.c ${_pbc_out})
    target_include_directories(bench_protobuf_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GEN}/pbc)
    target_link_libraries(bench_protobuf_c PRIVATE PkgConfig::PROTOBUF_C)
    target_compile_options(bench_protobuf_c PRIVATE ${_flags})
    list(APPEND _benches bench_protobuf_c)
else()
    message(STATUS "protobuf-c not found, skipped")
endif()

# nanopb: the generator from the system and its runtime sources at the
# generator's own version, compiled here because pb_realloc and pb_free are
# set at compile time (nanopb_alloc.h). -DNANOPB_SOURCE_DIR=<dir> takes a
# checkout; otherwise the release is downloaded, and without one nanopb is n/a.
find_program(PROTOC_GEN_NANOPB protoc-gen-nanopb)
find_program(NANOPB_GENERATOR NAMES nanopb_generator nanopb_generator.py)
if(PROTOC_GEN_NANOPB AND NANOPB_GENERATOR AND NOT NANOPB_SOURCE_DIR)
    execute_process(COMMAND ${NANOPB_GENERATOR} --version
                    OUTPUT_VARIABLE _npb_version OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "[0-9][0-9.]*$" _npb_version "${_npb_version}")
    set(_npb_dir ${CMAKE_CURRENT_BINARY_DIR}/nanopb-${_npb_version})
    if(_npb_version AND NOT EXISTS ${_npb_dir}/pb_decode.c)
        set(_npb_tar ${CMAKE_CURRENT_BINARY_DIR}/nanopb-${_npb_version}.tar.gz)
        file(DOWNLOAD https://github.com/nanopb/nanopb/archive/refs/tags/${_npb_version}.tar.gz
             ${_npb_tar} STATUS _npb_status)
        list(GET _npb_status 0 _npb_code)
        if(_npb_code EQUAL 0)
            file(ARCHIVE_EXTRACT INPUT ${_npb_tar} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/nanopb-x)
            file(GLOB _npb_top ${CMAKE_CURRENT_BINARY_DIR}/nanopb-x/*)
            file(RENAME ${_npb_top} ${_npb_dir})
            file(REMOVE_RECURSE ${CMAKE_CURRENT_BINARY_DIR}/nanopb-x)
        endif()
    endif()
    if(EXISTS ${_npb_dir}/pb_decode.c)
        set(NANOPB_SOURCE_DIR ${_npb_dir})
    endif()
endif()
if(PROTOC_GEN_NANOPB AND NANOPB_SOURCE_DIR)
    set(nanopb_SOURCE_DIR ${NANOPB_SOURCE_DIR})
    file(MAKE_DIRECTORY ${GEN}/nanopb)
    add_custom_command(
        OUTPUT ${GEN}/nanopb/benchmark.pb.c
        COMMAND ${PROTOC} -I ${CORPUS_DIR} --plugin=protoc-gen-nanopb=${PROTOC_GEN_NANOPB}
                --nanopb_opt=-f${CMAKE_CURRENT_SOURCE_DIR}/benchmark.options
                --nanopb_out=${GEN}/nanopb benchmark.proto
        DEPENDS ${CORPUS_DIR}/benchmark.proto ${CMAKE_CURRENT_SOURCE_DIR}/benchmark.options
        VERBATIM)
    add_executable(bench_nanopb bench_nanopb.c ${GEN}/nanopb/benchmark.pb.c
        ${nanopb_SOURCE_DIR}/pb_common.c ${nanopb_SOURCE_DIR}/pb_decode.c
        ${nanopb_SOURCE_DIR}/pb_encode.c)
    target_include_directories(bench_nanopb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
        ${GEN}/nanopb ${nanopb_SOURCE_DIR})
    target_compile_options(bench_nanopb PRIVATE ${_flags}
        -include ${CMAKE_CURRENT_SOURCE_DIR}/nanopb_alloc.h)
    list(APPEND _benches bench_nanopb)
else()
    message(STATUS "nanopb not found, skipped")
endif()

foreach(_t bench_cpp bench_upb bench_protobuf_c bench_nanopb)
    if(TARGET ${_t})
        target_link_options(${_t} PRIVATE ${_lto})
    endif()
endforeach()

# prost, built by cargo with the protoc above. Its Cargo.toml sets LTO;
# target-cpu=native is the counterpart of -march=native.
find_program(CARGO cargo)
if(CARGO)
    add_custom_target(bench_prost
        COMMAND ${CMAKE_COMMAND} -E env "RUSTFLAGS=$ENV{RUSTFLAGS} -Ctarget-cpu=native" PROTOC=${PROTOC}
                ${CARGO} build --release --locked --quiet
                --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/prost/Cargo.toml
                --target-dir ${CMAKE_CURRENT_BINARY_DIR}/prost
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/prost/release/bench_prost
                ${CMAKE_CURRENT_BINARY_DIR}/bench_prost
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/bench_prost
        VERBATIM)
    list(APPEND _benches bench_prost)
else()
    message(STATUS "prost (cargo) not found, skipped")
endif()

add_custom_target(bench_all DEPENDS ${_benches})
