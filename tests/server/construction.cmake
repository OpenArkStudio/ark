# Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
foreach(argument PROGRAM ROLE)
    if(NOT DEFINED ${argument})
        message(FATAL_ERROR "${argument} is required")
    endif()
endforeach()
execute_process(COMMAND "${PROGRAM}" "${ROLE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 20)
# A role calling exit(0) midway through its lifecycle is a regression, even
# though the process status is zero. Require the final C++ assertion marker.
set(completion "${ROLE}: actual factory, interface key, disabled update, duplicate rejection, reinstall and startup failure propagation passed")
string(FIND "${output}" "${completion}" completed)
if(NOT "${result}" STREQUAL "0" OR completed EQUAL -1)
    message(FATAL_ERROR "Server construction/startup regression failed: ${result}\n${output}\n${errors}")
endif()
message(STATUS "${output}")
