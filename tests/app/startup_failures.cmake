# Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0

foreach(argument PROGRAM PLUGIN_DIR SOURCE_ROOT RUNTIME_DIR)
    if(NOT DEFINED ${argument})
        message(FATAL_ERROR "${argument} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${RUNTIME_DIR}")
file(TO_CMAKE_PATH "${PLUGIN_DIR}" plugin_directory)
string(APPEND plugin_directory "/")
string(REPLACE "&" "&amp;" xml_plugin_directory "${plugin_directory}")
string(REPLACE "\"" "&quot;" xml_plugin_directory "${xml_plugin_directory}")
string(REPLACE "<" "&lt;" xml_plugin_directory "${xml_plugin_directory}")
string(REPLACE "\"" "\\\"" toml_plugin_directory "${plugin_directory}")
set(plugin_name ark_app_startup_failure_plugin)

# Exercise the production argument entry point and inherited native working
# directory. Non-BMP text cannot round-trip through legacy Windows code pages.
# The fixture checks the manager's application-name bytes before failing init.
set(unicode_runtime "${RUNTIME_DIR}/运行-é-🧪 with spaces")
file(MAKE_DIRECTORY "${unicode_runtime}/清单-é-🧪")
foreach(format toml app.conf)
    set(relative_manifest "清单-é-🧪/应用-é-🧪.${format}")
    set(manifest "${unicode_runtime}/${relative_manifest}")
    if(format STREQUAL "toml")
        file(WRITE "${manifest}"
            "[application]\nplugin_library_dir = \"${toml_plugin_directory}\"\n"
            "[logging]\nconsole = true\nfile = false\npattern = '%v'\n"
            "[[plugins]]\nname = \"${plugin_name}\"\nconfig = 'utf8:init'\n")
    else()
        file(WRITE "${manifest}"
            "<xml><plugins plugin_lib_dir=\"${xml_plugin_directory}\">"
            "<plugin name=\"${plugin_name}\" conf=\"utf8:init\"/></plugins>"
            "<logging console=\"true\" file=\"false\" pattern=\"%v\"/></xml>\n")
    endif()
    foreach(path_kind absolute relative)
        if(path_kind STREQUAL "absolute")
            set(arguments "--name=应用-é-🧪" "--app_conf=${manifest}")
        else()
            set(arguments -n "应用-é-🧪" -a "${relative_manifest}")
        endif()
        execute_process(COMMAND "${PROGRAM}" --busid=1.0.1.1 ${arguments}
            WORKING_DIRECTORY "${unicode_runtime}"
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
        file(WRITE "${manifest}.${path_kind}.stdout.log" "${output}")
        file(WRITE "${manifest}.${path_kind}.stderr.log" "${errors}")
        file(WRITE "${manifest}.${path_kind}.result.txt" "${result}\n")
        if(NOT result STREQUAL "1" OR
                NOT errors MATCHES "Application startup failed at init: app_startup_failure_module")
            message(FATAL_ERROR "UTF-8 ${path_kind}/${format}: expected controlled init failure\n${result}\n${output}\n${errors}")
        endif()
        set(expected "app-startup-test:unicode_arguments_verified;app-startup-test:owned_io_started;app-startup-test:init")
        string(REGEX MATCHALL "app-startup-test:[a-z_]+" observed "${errors}")
        if(NOT "${observed}" STREQUAL "${expected}")
            message(FATAL_ERROR "UTF-8 ${path_kind}/${format}: argument verification or lifecycle differed\n${errors}")
        endif()
        message(STATUS "UTF-8 ${path_kind}/${format}: Unicode manifest, working directory and application name verified")
    endforeach()
endforeach()

foreach(mode false:init false:post_init false:check_config false:pre_update
    throw:init throw:post_init throw:check_config throw:pre_update unknown:init throw:load_plugin missing_library:load_plugin missing_entry:load_plugin)
    string(REPLACE ":" ";" parts "${mode}")
    list(GET parts 0 behavior)
    list(GET parts 1 failed_phase)
    if(behavior STREQUAL "missing_library")
        set(case_plugin ark_app_missing_library_plugin)
    elseif(behavior STREQUAL "missing_entry")
        set(case_plugin ark_app_missing_entry_plugin)
    else()
        set(case_plugin "${plugin_name}")
    endif()
    foreach(format toml app.conf)
        set(manifest "${RUNTIME_DIR}/${behavior}-${failed_phase}.${format}")
        if(format STREQUAL "toml")
            file(WRITE "${manifest}"
                "[application]\nplugin_library_dir = \"${toml_plugin_directory}\"\n"
                "[[plugins]]\nname = \"${case_plugin}\"\nconfig = \"${mode}\"\n")
        else()
            file(WRITE "${manifest}"
                "<xml><plugins plugin_lib_dir=\"${xml_plugin_directory}\">"
                "<plugin name=\"${case_plugin}\" conf=\"${mode}\"/></plugins></xml>\n")
        endif()
        execute_process(
            COMMAND "${PROGRAM}" --busid=1.0.1.1 --name=startup-error-test
                "--app_conf=${manifest}"

            WORKING_DIRECTORY "${RUNTIME_DIR}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE errors
            TIMEOUT 15)
        file(WRITE "${manifest}.stdout.log" "${output}")
        file(WRITE "${manifest}.stderr.log" "${errors}")
        file(WRITE "${manifest}.result.txt" "${result}\n")
        if(NOT result STREQUAL "1")
            message(FATAL_ERROR "${failed_phase}/${format}: expected normal exit 1, got ${result}\n${output}\n${errors}")
        endif()
        if(failed_phase STREQUAL "load_plugin")
            set(owner "${case_plugin}")
        else()
            set(owner app_startup_failure_module)
        endif()
        if(NOT errors MATCHES "Application startup failed at ${failed_phase}: ${owner}")
            message(FATAL_ERROR "${mode}/${format}: missing phase/module-or-plugin startup diagnostic\n${errors}")
        endif()
        string(REGEX MATCHALL "Application startup failed" diagnostics "${errors}")
        list(LENGTH diagnostics count)
        if(NOT count EQUAL 1)
            message(FATAL_ERROR "${mode}/${format}: startup failure must be reported once")
        endif()
        if(behavior STREQUAL "throw")
            if(failed_phase STREQUAL "load_plugin")
                set(exception "test plugin entry exception")
            else()
                set(exception "test startup exception in ${failed_phase}")
            endif()
            if(NOT errors MATCHES "${exception}")
                message(FATAL_ERROR "${mode}/${format}: exception details were lost\n${errors}")
            endif()
        elseif(behavior STREQUAL "unknown" AND NOT errors MATCHES "unknown exception")
            message(FATAL_ERROR "${mode}/${format}: unknown exception was not identified\n${errors}")
        endif()
        set(expected "")
        if(NOT behavior MATCHES "^missing_(entry|library)$")
            if(NOT errors MATCHES "app-startup-test:owned_io_started port=[1-9][0-9]*")
                message(FATAL_ERROR "${mode}/${format}: real owned network endpoint did not start\n${errors}")
            endif()
            list(APPEND expected "app-startup-test:owned_io_started")
        endif()
        if(behavior STREQUAL "missing_entry")
            list(APPEND expected "app-startup-test:library_loaded")
        elseif(behavior STREQUAL "missing_library")
            # No module or library initializer can run for a missing file.
        elseif(failed_phase STREQUAL "load_plugin")
            list(APPEND expected "app-startup-test:load_plugin")
        else()
            foreach(phase init post_init check_config pre_update)
                list(APPEND expected "app-startup-test:${phase}")
                if(phase STREQUAL failed_phase)
                    break()
                endif()
            endforeach()
        endif()
        string(REGEX MATCHALL "app-startup-test:[a-z_]+" observed "${errors}")
        if(NOT "${observed}" STREQUAL "${expected}")
            message(FATAL_ERROR "${failed_phase}/${format}: unexpected lifecycle calls\nexpected=${expected}\nobserved=${observed}\n${errors}")
        endif()
        message(STATUS "${mode}/${format}: diagnostic and normal exit 1; no later phase, update, shutdown or uninstall call")
    endforeach()
endforeach()
