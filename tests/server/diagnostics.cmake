# Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
foreach(argument PROGRAM PROBE PLUGIN_DIR SOURCE_ROOT RUNTIME_ROOT)
    if(NOT DEFINED ${argument})
        message(FATAL_ERROR "${argument} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${RUNTIME_ROOT}")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef suffix)
execute_process(COMMAND "${PROGRAM}" "${PROBE}" "${PLUGIN_DIR}" "${SOURCE_ROOT}" "${RUNTIME_ROOT}/${suffix}" login healthy
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 20)
if(NOT "${result}" STREQUAL "1" OR NOT errors MATCHES "Launcher exited before readiness"
        OR NOT errors MATCHES "Child exit code 66"
        OR NOT errors MATCHES "Controlled startup child exited before readiness")
    message(FATAL_ERROR "Early child diagnostic was lost: ${result}\n${output}\n${errors}")
endif()
message(STATUS "Launcher preserves owned child exit status and diagnostic on early failure")
