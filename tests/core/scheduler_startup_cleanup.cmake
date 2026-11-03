# Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
if(NOT DEFINED PROGRAM)
    message(FATAL_ERROR "PROGRAM is required")
endif()

# Bound only these owned test processes. A marker followed by a hang still fails.
execute_process(COMMAND "${PROGRAM}" TIMEOUT 20
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
message("${output}${errors}")
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Schedule startup cleanup regressions failed: ${result}")
endif()

execute_process(COMMAND "${PROGRAM}" --allocation-failure TIMEOUT 10
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
message("${output}${errors}")
if(NOT "${result}" STREQUAL "1" OR NOT "${errors}" MATCHES
    "Schedule startup allocation failure: dispatcher retired; workers joined; candidate gone; exit 1")
    message(FATAL_ERROR "Schedule startup allocation failure regression failed: ${result}")
endif()
