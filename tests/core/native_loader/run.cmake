foreach(required PROGRAM PLUGIN PLUGIN_DIR DEPENDENCY WORKING_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
# The new fixture directory is never added to PATH or a process-wide DLL search
# setting. Only dynamic_library's per-load path may locate the sibling dependency.
cmake_path(CONVERT "$ENV{PATH}" TO_CMAKE_PATH_LIST search_paths NORMALIZE)
function(run_native_loader_case label plugin_directory dependency working_directory)
    cmake_path(NORMAL_PATH plugin_directory OUTPUT_VARIABLE normalized_plugins)
    if(normalized_plugins IN_LIST search_paths)
        message(FATAL_ERROR "Plugin fixture directory must not be in PATH")
    endif()
    file(MAKE_DIRECTORY "${working_directory}")
    execute_process(COMMAND "${PROGRAM}" "${plugin_directory}" "${dependency}" WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "Native ${label} sibling dependency regression failed: ${result}\n${output}\n${errors}")
    endif()
    message(STATUS "${label}: ${output}")
endfunction()

run_native_loader_case(ASCII "${PLUGIN_DIR}" "${DEPENDENCY}" "${WORKING_DIR}")

# Copy only the real sibling/dependent fixture DSOs. Keep host, working directory
# and plugin directory distinct, including a non-BMP path passed through argv.
set(unicode_root "${WORKING_DIR}/Unicode 插件 🚀")
set(unicode_plugins "${unicode_root}/plugins")
file(MAKE_DIRECTORY "${unicode_plugins}")
get_filename_component(dependency_name "${DEPENDENCY}" NAME)
foreach(library IN ITEMS "${PLUGIN}" "${DEPENDENCY}")
    file(COPY "${library}" DESTINATION "${unicode_plugins}")
endforeach()
run_native_loader_case(Unicode "${unicode_plugins}" "${unicode_plugins}/${dependency_name}"
    "${WORKING_DIR}/Unicode 工作 🚀")
