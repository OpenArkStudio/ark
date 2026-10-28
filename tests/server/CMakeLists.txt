# Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
# Keep this host free of the aggregate SDK and generated protocol objects. Each
# DSO inspection runs in its own process, separate from the real-factory host,
# so static AFProto descriptors cannot register twice in one process.
add_executable(ark_server_exports_tests exports.cpp)
set_property(TARGET ark_server_exports_tests PROPERTY LINK_LIBRARIES ark::core)

add_custom_target(check-server-plugins)
foreach(server game login proxy world master dir router)
    set(source "${PROJECT_SOURCE_DIR}/src/server/${server}")
    configure_file(types.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/${server}/server_test_types.hpp" @ONLY)
    set(test_target ark_${server}_construction_tests)
    # Compile the real private factory and assembly into the test as well: Windows
    # does not export those C++ internals from the runtime DLL.
    add_executable(${test_target} construction.cpp
        "${source}/${server}_plugin.cpp" "${source}/${server}_net_module_impl.cpp")
    set_property(TARGET ${test_target} PROPERTY LINK_LIBRARIES ark::ark ark_server_compat_headers)
    target_include_directories(${test_target} PRIVATE "${source}" "${CMAKE_CURRENT_BINARY_DIR}/${server}")
    add_dependencies(${test_target} ark_${server}_plugin)
    add_custom_target(check-server-${server}
        COMMAND $<TARGET_FILE:ark_server_exports_tests> "$<TARGET_FILE_DIR:ark_${server}_plugin>/" "ark_${server}_plugin"
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:${test_target}>" "-DROLE=ark_${server}_plugin"
            -P "${PROJECT_SOURCE_DIR}/tests/server/construction.cmake"
        DEPENDS ark_server_exports_tests ${test_target} ark_${server}_plugin USES_TERMINAL VERBATIM)
    add_dependencies(check-server-plugins check-server-${server})
endforeach()

# A counted substitute verifies rejected-factory ownership through the actual
# Login assembly. This does not replace the real-factory tests above.
add_executable(ark_server_registration_tests registration.cpp
    "${PROJECT_SOURCE_DIR}/src/server/login/login_plugin.cpp")
set_property(TARGET ark_server_registration_tests PROPERTY LINK_LIBRARIES ark::ark ark_server_compat_headers)
target_include_directories(ark_server_registration_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/server/login")
add_custom_target(check-server-registration
    COMMAND $<TARGET_FILE:ark_server_registration_tests>
    DEPENDS ark_server_registration_tests USES_TERMINAL)
add_dependencies(check-server-plugins check-server-registration)

# Exercise the actual Login entry/exit functions through the real manager loader
# with a controlled module, without invoking business network initialization.
add_library(ark_server_entry_probe SHARED entry_factory.cpp
    "${PROJECT_SOURCE_DIR}/src/server/login/login_plugin.cpp")
set_property(TARGET ark_server_entry_probe PROPERTY LINK_LIBRARIES ark::ark)
target_include_directories(ark_server_entry_probe PRIVATE "${PROJECT_SOURCE_DIR}/src/server/login")
set_target_properties(ark_server_entry_probe PROPERTIES PREFIX "")
add_executable(ark_server_entry_tests entry.cpp)
set_property(TARGET ark_server_entry_tests PROPERTY LINK_LIBRARIES ark::ark ark_server_compat_headers)
add_custom_target(check-server-entry
    COMMAND $<TARGET_FILE:ark_server_entry_tests> "$<TARGET_FILE_DIR:ark_server_entry_probe>/" "ark_server_entry_probe"
    DEPENDS ark_server_entry_tests ark_server_entry_probe USES_TERMINAL VERBATIM)
add_dependencies(check-server-plugins check-server-entry)

# Exercise the actual launcher and supported local composition, without a
# registry service or business protocol doubles. This controller owns its child.
if(UNIX AND TARGET app AND TARGET ark_utility_plugin AND TARGET ark_bus_plugin AND TARGET ark_net_plugin)
    add_executable(ark_server_launcher_tests launcher.cpp)
    set_property(TARGET ark_server_launcher_tests PROPERTY LINK_LIBRARIES "")
    add_custom_target(check-server-launcher
        COMMAND ${CMAKE_COMMAND}
            "-DPROGRAM=$<TARGET_FILE:ark_server_launcher_tests>"
            "-DLAUNCHER=$<TARGET_FILE:app>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_login_plugin>"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DRUNTIME_ROOT=${CMAKE_CURRENT_BINARY_DIR}/launcher runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/server/launcher.cmake"
        DEPENDS ark_server_launcher_tests app ark_utility_plugin ark_bus_plugin ark_net_plugin ark_login_plugin
        USES_TERMINAL VERBATIM)
    add_dependencies(check-server-plugins check-server-launcher)
    add_custom_target(check-server-startup)
    set(startup_roles login proxy router master world dir)
    if(TARGET ark_kernel_plugin)
        list(APPEND startup_roles game)
        add_custom_target(check-server-startup-game)
    endif()
    foreach(role IN LISTS startup_roles)
        set(startup_dependencies ark_server_launcher_tests app ark_utility_plugin ark_bus_plugin ark_net_plugin ark_${role}_plugin)
        if(role STREQUAL "game")
            list(APPEND startup_dependencies ark_kernel_plugin)
        endif()
        foreach(scenario healthy occupied)
            set(startup_target check-server-startup-${role}-${scenario})
            add_custom_target(${startup_target}
                COMMAND ${CMAKE_COMMAND}
                    "-DPROGRAM=$<TARGET_FILE:ark_server_launcher_tests>"
                    "-DLAUNCHER=$<TARGET_FILE:app>"
                    "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_${role}_plugin>"
                    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                    "-DRUNTIME_ROOT=${CMAKE_CURRENT_BINARY_DIR}/startup runtime"
                    "-DROLE=${role}" "-DSCENARIO=${scenario}"
                    -P "${PROJECT_SOURCE_DIR}/tests/server/launcher.cmake"
                DEPENDS ${startup_dependencies}
                USES_TERMINAL VERBATIM)
            add_dependencies(check-server-startup ${startup_target})
            if(role STREQUAL "game")
                add_dependencies(check-server-startup-game ${startup_target})
            endif()
        endforeach()
    endforeach()
    add_dependencies(check-server-plugins check-server-startup)
    add_executable(ark_server_early_exit_probe early_exit.cpp)
    set_property(TARGET ark_server_early_exit_probe PROPERTY LINK_LIBRARIES "")
    add_custom_target(check-server-launcher-diagnostics
        COMMAND ${CMAKE_COMMAND}
            "-DPROGRAM=$<TARGET_FILE:ark_server_launcher_tests>"
            "-DPROBE=$<TARGET_FILE:ark_server_early_exit_probe>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_login_plugin>"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DRUNTIME_ROOT=${CMAKE_CURRENT_BINARY_DIR}/diagnostic runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/server/diagnostics.cmake"
        DEPENDS ark_server_launcher_tests ark_server_early_exit_probe USES_TERMINAL VERBATIM)
    add_dependencies(check-server-plugins check-server-launcher-diagnostics)
endif()
