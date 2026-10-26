# Proves the read side of a generated header: every reader_init, getter and
# iterator it defines, plus the runtime decoders they call. The function list
# is read from the header itself, so a new field family needs no edit here.
#
#   cmake -DFRAMAC=... -DHEADER=... -DRUNTIME_INCLUDE=... -DREPORT=... -P verify_reader.cmake
#
# CALLERS, optional, is a C file of caller_* functions over the header,
# proved alongside it.

# string(JSON) below.
cmake_minimum_required(VERSION 3.19)

file(STRINGS "${HEADER}" _lines REGEX "^[A-Za-z_][A-Za-z0-9_]*\\(")
set(_fcts)
foreach(_line IN LISTS _lines)
    string(REGEX MATCH "^[A-Za-z_][A-Za-z0-9_]*" _name "${_line}")
    if(_name MATCHES "_(reader_init|count|begin|next)$" OR _name MATCHES "_reader_get_")
        list(APPEND _fcts ${_name})
    endif()
endforeach()
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

get_filename_component(_dir "${HEADER}" DIRECTORY)
get_filename_component(_name "${HEADER}" NAME)
set(_drive "${_dir}/verify_reader_drive.c")
file(WRITE "${_drive}" "#include \"${_name}\"\n")
if(CALLERS)
    file(APPEND "${_drive}" "#include \"${CALLERS}\"\n")
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
        -cpp-extra-args=-I${_dir}\ -I${RUNTIME_INCLUDE}
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
