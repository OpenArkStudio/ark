# Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
foreach(argument PROGRAM PLUGIN_DIR SOURCE_ROOT RUNTIME_DIR)
    if(NOT DEFINED ${argument})
        message(FATAL_ERROR "${argument} is required")
    endif()
endforeach()
file(TO_CMAKE_PATH "${PLUGIN_DIR}" plugin_directory)
file(MAKE_DIRECTORY "${RUNTIME_DIR}")
file(WRITE "${RUNTIME_DIR}/missing-provider.toml"
    "[application]\nplugin_library_dir = '${plugin_directory}/'\n"
    "[[plugins]]\nname = 'timers_plugin'\n")
execute_process(COMMAND "${PROGRAM}" --busid=1.0.1.1 --name=missing-provider
    "--app_conf=${RUNTIME_DIR}/missing-provider.toml"

    WORKING_DIRECTORY "${RUNTIME_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
file(WRITE "${RUNTIME_DIR}/output.log" "${output}\n${errors}")
if(NOT result STREQUAL "1")
    message(FATAL_ERROR "Missing required provider must exit 1, got ${result}\n${output}\n${errors}")
endif()
if(NOT errors MATCHES "Application startup failed at dependencies: .*timers_module.* requires missing module '.*(timer_module|guid_module).*'")
    message(FATAL_ERROR "Missing provider lost its consumer/provider context\n${errors}")
endif()
if("${output}\n${errors}" MATCHES "timers_module.*(, init|PostInit|PreShut|Shut)" OR
        "${output}\n${errors}" MATCHES "ARK Server stop")
    message(FATAL_ERROR "Invalid module graph entered startup/shutdown hooks\n${output}\n${errors}")
endif()
message(STATUS "Actual Timers DSO required-provider validation precedes init; launcher exits 1 without cleanup hooks")
