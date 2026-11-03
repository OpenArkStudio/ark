# Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
if(NOT DEFINED PROGRAM OR NOT DEFINED FIXTURE_DIR)
    message(FATAL_ERROR "PROGRAM and FIXTURE_DIR are required")
endif()
execute_process(COMMAND "${PROGRAM}" --install-failure "${FIXTURE_DIR}" TIMEOUT 10
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
message("${output}${errors}")
if(NOT "${result}" STREQUAL "1" OR NOT "${errors}" MATCHES
    "HTTP ownership install failure: candidate gone; native, captures and DSO retained; exit 1" OR
    "${errors}" MATCHES "forbidden HTTP ownership fixture")
    message(FATAL_ERROR "HTTP startup ownership install regression failed: ${result}")
endif()
execute_process(COMMAND "${PROGRAM}" TIMEOUT 20
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
message("${output}${errors}")
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "HTTP startup ownership regressions failed: ${result}")
endif()
execute_process(COMMAND "${PROGRAM}" --allocation-failure TIMEOUT 10
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
message("${output}${errors}")
if(NOT "${result}" STREQUAL "1" OR NOT "${errors}" MATCHES
    "HTTP ownership failure runner reached: native workers and older owners retained; exit 1" OR
    "${errors}" MATCHES "forbidden native destruction")
    message(FATAL_ERROR "HTTP startup ownership allocation regression failed: ${result}")
endif()
