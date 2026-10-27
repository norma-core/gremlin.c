# Proves every reader function of a generated header and the headers it includes.
#   cmake -DFRAMAC=... -DHEADER=... -DRUNTIME_INCLUDE=... -DREPORT=... [-DOUT_ROOT=<generator -o dir>] [-DCALLERS=<caller_* .c>] -P verify_reader.cmake

# string(JSON) below.
cmake_minimum_required(VERSION 3.19)

get_filename_component(_dir "${HEADER}" DIRECTORY)
# Generated includes are relative to the generator's output directory.
if(NOT OUT_ROOT)
    set(OUT_ROOT "${_dir}")
endif()
set(_headers "${HEADER}")
set(_fcts)
set(_i 0)
list(LENGTH _headers _n)
while(_i LESS _n)
    list(GET _headers ${_i} _h)
    file(STRINGS "${_h}" _incs REGEX "^#include \"[^\"]+\\.pb\\.h\"")
    foreach(_inc IN LISTS _incs)
        string(REGEX REPLACE "^#include \"([^\"]+)\".*" "${OUT_ROOT}/\\1" _inc "${_inc}")
        if(NOT _inc IN_LIST _headers)
            list(APPEND _headers "${_inc}")
        endif()
    endforeach()
    # Readers take `r` or `it` first, whatever the mangler named them;
    # writers take `m`. Anything else would silently go unproved.
    file(STRINGS "${_h}" _defs REGEX "^[A-Za-z_][A-Za-z0-9_]*\\(")
    foreach(_line IN LISTS _defs)
        string(REGEX MATCH "^[A-Za-z_][A-Za-z0-9_]*" _name "${_line}")
        if(_line MATCHES "^[A-Za-z_][A-Za-z0-9_]*\\((const )?[A-Za-z_][A-Za-z0-9_]* \\*(r|it)[,)]")
            list(APPEND _fcts ${_name})
        elseif(NOT _line MATCHES "\\*m[,)]" AND NOT _name STREQUAL "__attribute__")
            message(FATAL_ERROR "neither a reader nor a writer: ${_line}")
        endif()
    endforeach()
    math(EXPR _i "${_i} + 1")
    list(LENGTH _headers _n)
endwhile()
if(NOT _fcts)
    message(FATAL_ERROR "no reader functions found in ${HEADER}")
endif()
if(CALLERS)
    file(STRINGS "${CALLERS}" _callers REGEX "^caller_[A-Za-z0-9_]*\\(")
    foreach(_line IN LISTS _callers)
        string(REGEX MATCH "^caller_[A-Za-z0-9_]*" _name "${_line}")
        list(APPEND _fcts ${_name})
    endforeach()
endif()
list(APPEND _fcts
    gremlin_varint32_decode gremlin_varint_decode gremlin_bytes_decode
    gremlin_skip_data gremlin_fixed32_decode gremlin_fixed64_decode
    gremlin_unzigzag32 gremlin_unzigzag64 gremlin_f32_from_bits
    gremlin_f64_from_bits)
string(REPLACE ";" "," _fcts_arg "${_fcts}")

get_filename_component(_name "${HEADER}" NAME)
set(_drive "${_dir}/verify_reader_drive.c")
file(WRITE "${_drive}" "#include \"${_name}\"\n")
set(_callers_dir)
if(CALLERS)
    get_filename_component(_callers_dir "${CALLERS}" DIRECTORY)
    get_filename_component(_callers_name "${CALLERS}" NAME)
    file(APPEND "${_drive}" "#include \"${_callers_name}\"\n")
endif()
set(_cpp "-I${_dir} -I${OUT_ROOT} -I${RUNTIME_INCLUDE}")
if(_callers_dir)
    string(APPEND _cpp " -I${_callers_dir}")
endif()

include(ProcessorCount)
ProcessorCount(_nproc)
if(_nproc EQUAL 0)
    set(_nproc 4)
endif()

file(REMOVE "${REPORT}")
execute_process(
    COMMAND ${FRAMAC}
        -machdep gcc_x86_64
        -keep-unused-functions all
        -wp -wp-rte -wp-timeout 120
        -wp-split
        -wp-prover alt-ergo,z3
        -wp-par ${_nproc}
        -wp-cache none
        -wp-smoke-tests
        -wp-msg-key shell
        -kernel-warn-key annot:missing-spec=inactive
        -wp-warn-key pedantic-assigns=inactive
        -wp-no-warn-memory-model
        -cpp-extra-args=${_cpp}
        -wp-report-json ${REPORT}
        -wp-fct ${_fcts_arg}
        ${_drive}
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "frama-c failed: ${_rc}")
endif()

# frama-c exits 0 whether or not it proved anything; the report decides.
file(READ "${REPORT}" _json)
string(JSON _total LENGTH "${_json}")
if(_total EQUAL 0)
    message(FATAL_ERROR "no goals in ${REPORT}")
endif()
math(EXPR _last "${_total} - 1")
set(_failed 0)
foreach(_i RANGE ${_last})
    string(JSON _passed GET "${_json}" ${_i} passed)
    if(NOT _passed)
        string(JSON _goal GET "${_json}" ${_i} goal)
        message(STATUS "not proved: ${_goal}")
        math(EXPR _failed "${_failed} + 1")
    endif()
endforeach()
if(_failed GREATER 0)
    message(FATAL_ERROR "${_failed} of ${_total} goals not proved")
endif()
message(STATUS "${_total} goals proved")
