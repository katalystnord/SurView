# Configure-time engine pin check -- ADVISORY.
#
# Reports whether the OpenCorr checkout SurView will build against is the exact
# commit recorded in cmake/opencorr.pin. It warns and continues; it never fails
# the configure. That is deliberate: the engine fork and SurView are both moving
# fast, and iterating on the two together would otherwise mean a broken build
# every time the engine is one commit ahead. Revisit once the engine settles.
#
# Deliberately offline and instant: it inspects a local checkout and never
# fetches. The full three-way check (upstream vs fork, our upstreamed fixes by
# content, pin vs HEAD) needs the network and lives in tools/check-engine.sh.

set(SURVIEW_OPENCORR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../OpenCorr"
    CACHE PATH "Path to the katalystnord/OpenCorr checkout SurView builds against")

# --- read the pin (single source of truth, shared with tools/check-engine.sh) ---
set(_pin_file "${CMAKE_CURRENT_LIST_DIR}/opencorr.pin")
file(STRINGS "${_pin_file}" _pin_lines REGEX "^SURVIEW_OPENCORR_[A-Z_]+=")
foreach(_line IN LISTS _pin_lines)
    if(_line MATCHES "^(SURVIEW_OPENCORR_[A-Z_]+)=(.*)$")
        set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endif()
endforeach()

if(NOT SURVIEW_OPENCORR_PIN)
    message(WARNING "Engine pin: could not read SURVIEW_OPENCORR_PIN from ${_pin_file}")
    return()
endif()

string(SUBSTRING "${SURVIEW_OPENCORR_PIN}" 0 7 _pin_short)

# --- locate the checkout ---
if(NOT IS_DIRECTORY "${SURVIEW_OPENCORR_DIR}/.git")
    message(STATUS "Engine pin: pinned to ${_pin_short} (${SURVIEW_OPENCORR_PIN_NOTE})")
    message(STATUS "Engine pin: no OpenCorr checkout at ${SURVIEW_OPENCORR_DIR} -- not checked.")
    message(STATUS "            Set -DSURVIEW_OPENCORR_DIR=<path> to point at yours.")
    return()
endif()

find_package(Git QUIET)
if(NOT Git_FOUND)
    message(STATUS "Engine pin: git not found -- pin ${_pin_short} not checked.")
    return()
endif()

# --- compare pin against the checkout's HEAD ---
execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${SURVIEW_OPENCORR_DIR}"
    OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE _rc)

if(NOT _rc EQUAL 0)
    message(STATUS "Engine pin: could not read HEAD of ${SURVIEW_OPENCORR_DIR} -- not checked.")
    return()
endif()

if(_head STREQUAL SURVIEW_OPENCORR_PIN)
    message(STATUS "Engine pin: OK -- OpenCorr at ${_pin_short} (${SURVIEW_OPENCORR_PIN_NOTE})")
    return()
endif()

# --- mismatch: say which way it moved, since that is what decides the fix ---
string(SUBSTRING "${_head}" 0 7 _head_short)

# is the pinned commit even in this checkout? if not, the pin is unresolvable
# here (wrong repo, or a fetch is needed) and ahead/behind would be meaningless
execute_process(
    COMMAND "${GIT_EXECUTABLE}" cat-file -e "${SURVIEW_OPENCORR_PIN}^{commit}"
    WORKING_DIRECTORY "${SURVIEW_OPENCORR_DIR}"
    RESULT_VARIABLE _have_pin ERROR_QUIET)

if(NOT _have_pin EQUAL 0)
    message(WARNING
        "Engine pin MISMATCH: checkout is at ${_head_short}, pin is ${_pin_short}, "
        "and the pinned commit is not in ${SURVIEW_OPENCORR_DIR} at all.\n"
        "   Wrong repository, or it needs a fetch. Building against an unverified engine.")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-list --left-right --count "${SURVIEW_OPENCORR_PIN}...${_head}"
    WORKING_DIRECTORY "${SURVIEW_OPENCORR_DIR}"
    OUTPUT_VARIABLE _counts OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
string(REGEX MATCH "^([0-9]+)[ \t]+([0-9]+)$" _m "${_counts}")
set(_behind "${CMAKE_MATCH_1}")  # commits the pin has that the checkout does not
set(_ahead "${CMAKE_MATCH_2}")   # commits the checkout has beyond the pin

message(WARNING
    "Engine pin MISMATCH: building against OpenCorr ${_head_short}, pin is ${_pin_short}.\n"
    "   The checkout is ${_ahead} commit(s) ahead of the pin and missing ${_behind} of its commit(s).\n"
    "   If the engine moved on purpose, bump cmake/opencorr.pin as its own commit.\n"
    "   If not, check out the pin: git -C ${SURVIEW_OPENCORR_DIR} checkout ${_pin_short}\n"
    "   Full three-way check (needs network): tools/check-engine.sh")
