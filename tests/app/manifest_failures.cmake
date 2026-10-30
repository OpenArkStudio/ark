# Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0

foreach(argument PROGRAM SOURCE_ROOT RUNTIME_DIR)
    if(NOT DEFINED ${argument})
        message(FATAL_ERROR "${argument} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${RUNTIME_DIR}")
file(WRITE "${RUNTIME_DIR}/invalid.toml" "[application\n")
file(WRITE "${RUNTIME_DIR}/invalid.app.conf" "<xml><plugins></xml>")
file(WRITE "${RUNTIME_DIR}/wrong-type.toml" "[application]\nplugin_library_dir = 42\n")

foreach(manifest missing.toml invalid.toml invalid.app.conf wrong-type.toml)
    execute_process(
        COMMAND "${PROGRAM}" --busid=1.0.1.1 --name=manifest-error-test
            "--app_conf=${RUNTIME_DIR}/${manifest}"

        WORKING_DIRECTORY "${RUNTIME_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE errors
        TIMEOUT 15)
    if(NOT result STREQUAL "1")
        message(FATAL_ERROR "${manifest}: expected normal exit 1, got ${result}\n${output}\n${errors}")
    endif()
    if(NOT errors MATCHES "Application manifest loading failed:")
        message(FATAL_ERROR "${manifest}: missing manifest diagnostic\n${errors}")
    endif()
    message(STATUS "${manifest}: normal exit 1 with manifest diagnostic")
endforeach()

# Keep each logging check run independent from earlier launcher output.
# Preserve prior runs for diagnosis rather than deleting their files.
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef logging_run)
set(RUNTIME_DIR "${RUNTIME_DIR}/embedded-logging-${logging_run}")
file(MAKE_DIRECTORY "${RUNTIME_DIR}")
message(STATUS "Embedded logging runtime: ${RUNTIME_DIR}")

# Embedded logging errors are rejected before attempting plugin loads.
foreach(case bad-level bad-console bad-queue bad-sinks bad-path)
    if(case STREQUAL "bad-level")
        set(fields "level = 'invalid'\n")
    elseif(case STREQUAL "bad-console")
        set(fields "console = 'yes'\n")
    elseif(case STREQUAL "bad-queue")
        set(fields "queue_count = 65536\n")
    elseif(case STREQUAL "bad-sinks")
        set(fields "console = false\n")
    else()
        set(fields "output_path = ''\n")
    endif()
    set(manifest "${RUNTIME_DIR}/${case}.toml")
    file(WRITE "${manifest}" "[application]\n[logging]\n${fields}[[plugins]]\nname = 'never_load_this_plugin'\n")
    execute_process(COMMAND "${PROGRAM}" --busid=1.0.1.1 --name=logging-test "--app_conf=${manifest}"
        WORKING_DIRECTORY "${RUNTIME_DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
    if(NOT result STREQUAL "1" OR NOT errors MATCHES "${case}.toml.*logging" OR errors MATCHES "Application startup failed")
        message(FATAL_ERROR "${case}: expected logging schema error before plugin load\n${result}\n${output}\n${errors}")
    endif()
endforeach()

# No log.conf exists in this runtime. Defaults reach the real plugin loader and
# emit INFO output; an explicit file setting writes only below its configured path.
file(REMOVE "${RUNTIME_DIR}/conf/log.conf")
foreach(case default explicit)
    set(manifest "${RUNTIME_DIR}/${case}-logging.toml")
    set(logging "")
    if(case STREQUAL "explicit")
        set(logging "[logging]\nconsole = false\nfile = true\nlevel = 'info'\npattern = '%v'\noutput_path = 'logs with spaces/nested'\n")
    endif()
    file(WRITE "${manifest}" "[application]\n${logging}[[plugins]]\nname = 'never_load_this_plugin'\n")
    execute_process(COMMAND "${PROGRAM}" --busid=1.0.1.1 --name=logging-test "--app_conf=${manifest}"
        WORKING_DIRECTORY "${RUNTIME_DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
    if(NOT result STREQUAL "1" OR NOT errors MATCHES "Application startup failed at load_plugin: never_load_this_plugin")
        message(FATAL_ERROR "${case}: expected real plugin failure\n${result}\n${output}\n${errors}")
    endif()
    if(case STREQUAL "default" AND NOT output MATCHES "ARK Server start")
        message(FATAL_ERROR "default logger failed to emit startup INFO\n${output}")
    elseif(case STREQUAL "explicit" AND output MATCHES "ARK Server start")
        message(FATAL_ERROR "file-only logger unexpectedly emitted console INFO\n${output}")
    endif()
endforeach()
file(GLOB_RECURSE logs "${RUNTIME_DIR}/logs with spaces/nested/*.log")
if(NOT logs)
    message(FATAL_ERROR "explicit file logger did not create a log below its configured output_path")
endif()
set(found_start FALSE)
foreach(log IN LISTS logs)
    file(READ "${log}" contents)
    if(contents MATCHES "ARK Server start")
        set(found_start TRUE)
    endif()
endforeach()
if(NOT found_start)
    message(FATAL_ERROR "explicit synchronous file logger lost its startup diagnostic")
endif()
if(EXISTS "${RUNTIME_DIR}/binlog")
    message(FATAL_ERROR "default logger unexpectedly created a file output directory")
endif()

foreach(flag --log_conf -l)
    execute_process(COMMAND "${PROGRAM}" --busid=1.0.1.1 --name=logging-test
        "--app_conf=${RUNTIME_DIR}/default-logging.toml" "${flag}" missing-log.conf
        WORKING_DIRECTORY "${RUNTIME_DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
    if(NOT result STREQUAL "1" OR NOT errors MATCHES "has been removed; move logger settings into")
        message(FATAL_ERROR "${flag}: missing explicit migration error\n${result}\n${output}\n${errors}")
    endif()
endforeach()

# File opening errors identify logging initialization and never enter plugins.
file(WRITE "${RUNTIME_DIR}/not-a-directory" "occupied")
file(WRITE "${RUNTIME_DIR}/unwritable-logging.toml"
    "[application]\n[logging]\nfile = true\noutput_path = 'not-a-directory/child'\n[[plugins]]\nname = 'never_load_this_plugin'\n")
execute_process(COMMAND "${PROGRAM}" --busid=1.0.1.1 --name=logging-test
    "--app_conf=${RUNTIME_DIR}/unwritable-logging.toml"
    WORKING_DIRECTORY "${RUNTIME_DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
if(NOT result STREQUAL "1" OR NOT errors MATCHES "Application logging initialization failed.*unwritable-logging.toml" OR
        errors MATCHES "Application startup failed")
    message(FATAL_ERROR "file opening error escaped the logging startup boundary\n${result}\n${output}\n${errors}")
endif()
