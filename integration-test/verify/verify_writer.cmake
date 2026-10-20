# Proves the write side of a generated header: every _size and _cached_size it
# defines, the ghost helpers their proofs call, and the lemmas. Lemmas go in a
# second run: their induction needs the tip prover.
#
#   cmake -DFRAMAC=... -DHEADER=... -DRUNTIME_INCLUDE=... -DREPORT=... [-DPROTO=<.proto>] -P verify_writer.cmake

# string(JSON) below.
cmake_minimum_required(VERSION 3.19)

# Messages with write-side contracts are the ones that got ghost helpers. Their
# _size and _cached_size, the two that take only the message, are found by the
# type they take, whatever suffix they were given.
file(STRINGS "${HEADER}" _ghosts REGEX "^  void [A-Za-z_][A-Za-z0-9_]*\\(")
set(_fcts)
set(_types)
foreach(_line IN LISTS _ghosts)
    string(REGEX MATCH "^  void ([A-Za-z_][A-Za-z0-9_]*)\\(([A-Za-z_][A-Za-z0-9_]*) \\*m\\)" _m "${_line}")
    if(_m)
        list(APPEND _fcts ${CMAKE_MATCH_1})
        list(APPEND _types ${CMAKE_MATCH_2})
    elseif(_line MATCHES "^  void ([A-Za-z_][A-Za-z0-9_]*)\\(void\\)")
        list(APPEND _fcts ${CMAKE_MATCH_1})
    else()
        message(FATAL_ERROR "unexpected ghost: ${_line}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _types)
# A fixture expects every message to get contracts; one dropped from the plan
# would otherwise just prove fewer goals.
if(PROTO)
    file(READ "${PROTO}" _proto)
    string(REGEX REPLACE "//[^\n]*" "" _proto "${_proto}")
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])message[ \t\n]+[A-Za-z]" _msgs "${_proto}")
    list(LENGTH _msgs _nm)
    list(LENGTH _types _nt)
    if(NOT _nm EQUAL _nt)
        message(FATAL_ERROR "${_nt} of ${_nm} messages in ${PROTO} have write-side contracts")
    endif()
endif()
file(STRINGS "${HEADER}" _defs REGEX "^[A-Za-z_][A-Za-z0-9_]*\\(")
foreach(_t IN LISTS _types)
    set(_n 0)
    foreach(_line IN LISTS _defs)
        if(_line MATCHES "^([A-Za-z_][A-Za-z0-9_]*)\\(const ${_t} \\*m\\)")
            list(APPEND _fcts ${CMAKE_MATCH_1})
            math(EXPR _n "${_n} + 1")
        endif()
    endforeach()
    if(NOT _n EQUAL 2)
        message(FATAL_ERROR "${_t}: ${_n} of _size and _cached_size found, expected 2")
    endif()
endforeach()
if(NOT _fcts)
    message(FATAL_ERROR "no writer functions found in ${HEADER}")
endif()
string(REPLACE ";" "," _fcts_arg "${_fcts}")

get_filename_component(_dir "${HEADER}" DIRECTORY)
get_filename_component(_name "${HEADER}" NAME)
set(_drive "${_dir}/verify_writer_drive.c")
file(WRITE "${_drive}" "#include \"${_name}\"\n")

include(ProcessorCount)
ProcessorCount(_nproc)
# The slowest _size goals take up to a minute alone and miss the timeout once
# more than four provers run at once; a proof that depends on load is not one.
if(_nproc EQUAL 0 OR _nproc GREATER 4)
    set(_nproc 4)
endif()

set(_common
    -machdep gcc_x86_64
    -keep-unused-functions all
    -wp -wp-timeout 120
    -wp-split
    -wp-par ${_nproc}
    -wp-cache none
    -wp-msg-key shell
    -kernel-warn-key annot:missing-spec=inactive
    -wp-warn-key pedantic-assigns=inactive
    -wp-no-warn-memory-model
    -cpp-extra-args=-I${_dir}\ -I${RUNTIME_INCLUDE})

function(_run report)
    file(REMOVE "${report}")
    execute_process(
        COMMAND ${FRAMAC} ${_common} ${ARGN} -wp-report-json ${report} ${_drive}
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "frama-c failed: ${_rc}")
    endif()
    # frama-c exits 0 whether or not it proved anything; the report decides.
    file(READ "${report}" _json)
    string(JSON _total LENGTH "${_json}")
    if(_total EQUAL 0)
        message(FATAL_ERROR "no goals in ${report}")
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
    message(STATUS "${_total} goals proved (${report})")
endfunction()

_run("${REPORT}" -wp-rte -wp-smoke-tests -wp-prover alt-ergo,z3 -wp-fct ${_fcts_arg})
_run("${REPORT}.lemmas" -wp-prop=@lemma -wp-prover alt-ergo,z3,tip)
