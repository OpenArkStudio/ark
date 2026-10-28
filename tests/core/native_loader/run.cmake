foreach(required PROGRAM PLUGIN_DIR DEPENDENCY WORKING_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
# The new fixture directory is never added to PATH or a process-wide DLL search
# setting. Only dynamic_library's per-load path may locate the sibling dependency.
cmake_path(CONVERT "$ENV{PATH}" TO_CMAKE_PATH_LIST search_paths NORMALIZE)
cmake_path(NORMAL_PATH PLUGIN_DIR OUTPUT_VARIABLE normalized_plugins)
if(normalized_plugins IN_LIST search_paths)
    message(FATAL_ERROR "Plugin fixture directory must not be in PATH")
endif()
file(MAKE_DIRECTORY "${WORKING_DIR}")
execute_process(COMMAND "${PROGRAM}" "${PLUGIN_DIR}" "${DEPENDENCY}" WORKING_DIRECTORY "${WORKING_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Native sibling dependency regression failed: ${result}\n${output}\n${errors}")
endif()
message(STATUS "${output}")
