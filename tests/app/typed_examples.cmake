# Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
foreach(argument PROGRAM PLUGIN_DIR SOURCE_ROOT RUNTIME_DIR)
    if(NOT DEFINED ${argument})
        message(FATAL_ERROR "${argument} is required")
    endif()
endforeach()
foreach(sample timers module_lifecycle entity_operations)
    set(directory "${RUNTIME_DIR}/${sample}")
    file(MAKE_DIRECTORY "${directory}/conf" "${directory}/resource/meta")
    file(WRITE "${directory}/resource/meta/entity_schema.json" [=[
{"format":"ark.entity-schema","version":1,"classes":[{"name":"Player","fields":[],"tables":[],"containers":[]},{"name":"attribute","fields":[{"name":"id","index":1,"type":"uint32"},{"name":"level","index":2,"type":"int32"}],"tables":[],"containers":[]}],"configs":[{"class":"attribute","rows":[{"id":42,"level":7}]}]}
]=])
    if(sample STREQUAL "entity_operations")
        file(COPY "${SOURCE_ROOT}/examples/entity_operations/resource/" DESTINATION "${directory}/resource")
    endif()
    execute_process(COMMAND "${PROGRAM}" "${SOURCE_ROOT}" "${PLUGIN_DIR}" "${sample}"
        WORKING_DIRECTORY "${directory}" RESULT_VARIABLE result
        OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
    file(WRITE "${directory}/output.log" "${output}\n${errors}")
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${sample} actual DSO regression failed: ${result}\n${output}\n${errors}")
    endif()
    message(STATUS "${sample}: actual DSOs, five normal owner updates, cooperative drain, startup and shutdown passed")
endforeach()
