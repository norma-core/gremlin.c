# Proves the write side of a generated header: the _cached_size getters, the
# ghost helpers and the lemmas. Lemmas go in a second run: their induction
# needs the tip prover.
#
#   cmake -DFRAMAC=... -DHEADER=... -DRUNTIME_INCLUDE=... -DREPORT=... -P verify_writer.cmake

# string(JSON) below.
cmake_minimum_required(VERSION 3.19)

# Messages with write-side contracts are the ones that got a _tsize_nonneg ghost;
# their getters and every ghost helper go on the list.
file(STRINGS "${HEADER}" _ghosts REGEX "^  void [A-Za-z_][A-Za-z0-9_]*\\(")
set(_fcts)
foreach(_line IN LISTS _ghosts)
    string(REGEX MATCH "void [A-Za-z_][A-Za-z0-9_]*" _name "${_line}")
    string(SUBSTRING "${_name}" 5 -1 _name)
    list(APPEND _fcts ${_name})
    if(_name MATCHES "_tsize_nonneg_[0-9]+$")
        message(FATAL_ERROR "renamed ghost ${_name}: its message would be skipped")
    endif()
    if(_name MATCHES "^(.*)_tsize_nonneg$")
        set(_m ${CMAKE_MATCH_1})
        list(APPEND _fcts ${_m}_cached_size)
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
if(_nproc EQUAL 0)
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
