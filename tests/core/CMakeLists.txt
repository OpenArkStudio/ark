add_library(ark_core_header_checks OBJECT headers/queue.cpp headers/guid128.cpp headers/rw_lock.cpp
    headers/array_map.cpp headers/bit_value.cpp headers/consistent_hash.cpp headers/list.cpp
    headers/lock_free_queue.cpp headers/lru_map.cpp headers/map.cpp headers/thread_safe_queue.cpp
    headers/object_vector.cpp headers/error_code.cpp headers/logger.cpp headers/murmur_hash.cpp)
target_link_libraries(ark_core_header_checks PRIVATE ark::core)

add_subdirectory(hash_header)

add_executable(ark_core_tests main.cpp strings.cpp containers.cpp templates.cpp callbacks.cpp
    header_regressions.cpp cxx20_language.cpp concurrency.cpp plugin_manager_startup.cpp data_strings.cpp node_data_storage.cpp
    object_vectors.cpp plugin_construction.cpp module_factory.cpp random_generator.cpp xxtea.cpp murmur_hash.cpp)
target_link_libraries(ark_core_tests PRIVATE ark::core ark::kernel ark::utility)
add_dependencies(ark_core_tests ark_core_header_checks ark_murmur_hash_standalone_check)
add_library(ark_construction_header_checks OBJECT headers/module_base.cpp headers/plugin_base.cpp)
set_property(TARGET ark_construction_header_checks PROPERTY LINK_LIBRARIES ark::core)
add_dependencies(ark_core_tests ark_construction_header_checks)
if(ARK_BUILD_PROTO)
    target_sources(ark_core_tests PRIVATE protobuf_messages.cpp)
    target_link_libraries(ark_core_tests PRIVATE ark::bus ark::proto)
endif()
add_custom_target(check-core
    COMMAND $<TARGET_FILE:ark_core_tests>
    DEPENDS ark_core_tests
    USES_TERMINAL)

add_executable(ark_logging_tests main.cpp logging_paths.cpp logging_source.cpp)
target_link_libraries(ark_logging_tests PRIVATE ark::core)
add_dependencies(ark_logging_tests ark_core_header_checks)
add_custom_target(check-logging
    COMMAND $<TARGET_FILE:ark_logging_tests>
    DEPENDS ark_logging_tests
    USES_TERMINAL)
add_dependencies(check-core check-logging)

find_package(Threads REQUIRED)
add_library(ark_task_header_checks OBJECT headers/task.cpp)
# This SDK consumer uses the public core target without aggregate plugin APIs.
set_property(TARGET ark_task_header_checks PROPERTY LINK_LIBRARIES ark::core)

add_executable(ark_task_tests main.cpp tasks.cpp)
set_property(TARGET ark_task_tests PROPERTY LINK_LIBRARIES ark::core Threads::Threads)
target_include_directories(ark_task_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
add_dependencies(ark_task_tests ark_task_header_checks)
add_library(ark_task_scheduler_header_checks OBJECT headers/task_scheduler.cpp headers/scheduler_task.cpp)
set_property(TARGET ark_task_scheduler_header_checks PROPERTY LINK_LIBRARIES ark::core ark::utility)
add_dependencies(ark_task_tests ark_task_scheduler_header_checks)

add_executable(ark_task_contract_tests ../database/task_group_contract.cpp)
set_property(TARGET ark_task_contract_tests PROPERTY LINK_LIBRARIES ark::core Threads::Threads)
add_custom_target(check-task-sdk
    COMMAND $<TARGET_FILE:ark_task_tests>
    COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_task_contract_tests>"
        -P "${PROJECT_SOURCE_DIR}/tests/database/task_group_contract.cmake"
    DEPENDS ark_task_tests ark_task_contract_tests
    USES_TERMINAL)
add_dependencies(check-core check-task-sdk)

add_executable(ark_business_runtime_tests main.cpp business_runtime.cpp)
set_property(TARGET ark_business_runtime_tests PROPERTY LINK_LIBRARIES ark::core Threads::Threads)
target_include_directories(ark_business_runtime_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
add_library(ark_business_runtime_header_check OBJECT headers/business_runtime.cpp)
set_property(TARGET ark_business_runtime_header_check PROPERTY LINK_LIBRARIES ark::core)
add_dependencies(ark_business_runtime_tests ark_business_runtime_header_check)
add_executable(ark_business_runtime_contract_tests business_runtime_contract.cpp)
set_property(TARGET ark_business_runtime_contract_tests PROPERTY LINK_LIBRARIES ark::core Threads::Threads)
add_custom_target(check-business-runtime
    COMMAND $<TARGET_FILE:ark_business_runtime_tests>
    COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_business_runtime_contract_tests>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/business_runtime_contract.cmake"
    DEPENDS ark_business_runtime_tests ark_business_runtime_contract_tests USES_TERMINAL)
add_dependencies(check-core check-business-runtime)

add_executable(ark_app_shutdown_tests ../app/shutdown_request.cpp)
target_compile_features(ark_app_shutdown_tests PRIVATE cxx_std_20)
target_include_directories(ark_app_shutdown_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/application")
# This app-private state test does not consume the directory's inherited ark SDK.
set_property(TARGET ark_app_shutdown_tests PROPERTY LINK_LIBRARIES Threads::Threads)
add_custom_target(check-app-shutdown
    COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_app_shutdown_tests>"
        -P "${PROJECT_SOURCE_DIR}/tests/app/shutdown_request.cmake"
    DEPENDS ark_app_shutdown_tests
    USES_TERMINAL)
add_dependencies(check-core check-app-shutdown)

add_executable(ark_xml_tests main.cpp xml.cpp)
target_link_libraries(ark_xml_tests PRIVATE ark::core)
target_compile_definitions(ark_xml_tests PRIVATE ARK_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_custom_target(check-xml
    COMMAND $<TARGET_FILE:ark_xml_tests>
    DEPENDS ark_xml_tests
    USES_TERMINAL)
add_dependencies(check-core check-xml)

if(ARK_BUILD_CONFIG)
    add_library(ark_config_shared_probe SHARED ../consumer/config/shared.cpp)
    set_property(TARGET ark_config_shared_probe PROPERTY LINK_LIBRARIES ark::config tomlplusplus::tomlplusplus)
    target_include_directories(ark_config_shared_probe PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
    target_compile_definitions(ark_config_shared_probe PRIVATE ARK_CONFIG_PROBE_BUILD)

    add_executable(ark_application_manifest_tests main.cpp application_manifest.cpp ../consumer/config/shared_library.cpp)
    # These configuration-only consumers deliberately exclude the inherited network SDK.
    set_property(TARGET ark_application_manifest_tests PROPERTY LINK_LIBRARIES ark::config ark_config_shared_probe)
    target_compile_definitions(ark_application_manifest_tests PRIVATE ARK_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_custom_target(check-application-manifest
        COMMAND $<TARGET_FILE:ark_application_manifest_tests>
        DEPENDS ark_application_manifest_tests
        USES_TERMINAL)
    add_dependencies(check-core check-application-manifest)

    add_executable(ark_plugin_configuration_tests main.cpp plugin_configuration.cpp)
    set_property(TARGET ark_plugin_configuration_tests PROPERTY LINK_LIBRARIES ark::config tomlplusplus::tomlplusplus)
    target_include_directories(ark_plugin_configuration_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
    target_compile_features(ark_plugin_configuration_tests PRIVATE cxx_std_20)
    add_custom_target(check-plugin-configuration
        COMMAND $<TARGET_FILE:ark_plugin_configuration_tests>
        DEPENDS ark_plugin_configuration_tests USES_TERMINAL)
    add_dependencies(check-core check-plugin-configuration)

    add_executable(ark_bus_configuration_tests main.cpp bus_configuration.cpp
        ../../src/plugins/bus/bus_module_impl.cpp)
    set_property(TARGET ark_bus_configuration_tests PROPERTY LINK_LIBRARIES ark::bus ark::config tomlplusplus::tomlplusplus)
    target_include_directories(ark_bus_configuration_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
    target_compile_definitions(ark_bus_configuration_tests PRIVATE ARK_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_custom_target(check-bus-configuration
        COMMAND $<TARGET_FILE:ark_bus_configuration_tests>
        DEPENDS ark_bus_configuration_tests USES_TERMINAL)
    add_dependencies(check-core check-bus-configuration)

    if(TARGET app AND TARGET ark_bus_plugin AND TARGET ark_net_plugin AND TARGET ark_utility_plugin)
        add_custom_target(check-app-bus-configuration
            COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
                "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_bus_plugin>"
                "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/bus configuration app"
                -P "${PROJECT_SOURCE_DIR}/tests/app/bus_configuration.cmake"
            DEPENDS app ark_bus_plugin ark_net_plugin ark_utility_plugin USES_TERMINAL)
        add_dependencies(check-core check-app-bus-configuration)
    endif()

    if(TARGET app)
        add_library(ark_business_runtime_probe SHARED ../app/business_runtime_plugin.cpp)
        set_property(TARGET ark_business_runtime_probe PROPERTY LINK_LIBRARIES ark::core)
        target_include_directories(ark_business_runtime_probe PRIVATE "${PROJECT_SOURCE_DIR}/src/plugins")
        set_target_properties(ark_business_runtime_probe PROPERTIES PREFIX "")
        add_custom_target(check-app-business-runtime
            COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
                "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_business_runtime_probe>"
                "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/business runtime app"
                -P "${PROJECT_SOURCE_DIR}/tests/app/business_runtime.cmake"
            DEPENDS app ark_business_runtime_probe USES_TERMINAL)
        add_dependencies(check-core check-app-business-runtime)

        add_library(ark_app_startup_failure_plugin SHARED ../app/startup_failure_plugin.cpp)
        set_property(TARGET ark_app_startup_failure_plugin PROPERTY LINK_LIBRARIES ark::core)
        set_target_properties(ark_app_startup_failure_plugin PROPERTIES PREFIX "")
        add_library(ark_app_missing_entry_plugin SHARED ../app/missing_entry_plugin.cpp)
        set_property(TARGET ark_app_missing_entry_plugin PROPERTY LINK_LIBRARIES ark::core)
        set_target_properties(ark_app_missing_entry_plugin PROPERTIES PREFIX "")
        add_custom_target(check-app-startup-failures
            COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
                "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_app_startup_failure_plugin>"
                "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/startup failure runtime"
                -P "${PROJECT_SOURCE_DIR}/tests/app/startup_failures.cmake"
            DEPENDS app ark_app_startup_failure_plugin ark_app_missing_entry_plugin
            USES_TERMINAL)
        add_dependencies(check-core check-app-startup-failures)

        add_custom_target(check-app-manifest-failures
            COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
                "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/manifest failure runtime"
                -P "${PROJECT_SOURCE_DIR}/tests/app/manifest_failures.cmake"
            DEPENDS app
            USES_TERMINAL)
        add_dependencies(check-core check-app-manifest-failures)

        if(TARGET ark_utility_plugin AND TARGET timers_plugin)
            add_custom_target(check-app-presets
                COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:app>"
                    "-DUTILITY_PLUGIN=$<TARGET_FILE:ark_utility_plugin>"
                    "-DSAMPLE_PLUGIN=$<TARGET_FILE:timers_plugin>"
                    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
                    "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/preset runtime 'quoted' #"
                    -P "${PROJECT_SOURCE_DIR}/tests/app/application_presets.cmake"
                DEPENDS app ark_utility_plugin timers_plugin USES_TERMINAL VERBATIM)
            if(UNIX)
                add_executable(ark_app_preset_stop_tests ../app/application_preset_stop.cpp)
                set_property(TARGET ark_app_preset_stop_tests PROPERTY LINK_LIBRARIES Threads::Threads)
                target_compile_features(ark_app_preset_stop_tests PRIVATE cxx_std_20)
                add_custom_target(check-app-preset-stop
                    COMMAND $<TARGET_FILE:ark_app_preset_stop_tests>
                        "${PROJECT_SOURCE_DIR}/tests/app/application_preset_stop.sh"
                    DEPENDS ark_app_preset_stop_tests USES_TERMINAL VERBATIM)
                add_dependencies(check-app-presets check-app-preset-stop)
            endif()
            add_dependencies(check-core check-app-presets)
        endif()
    endif()
endif()

if(ARK_BUILD_PROTO)
    add_executable(ark_kernel_initialization_tests main.cpp module_initialization.cpp entity_config_copy.cpp
        ../../src/plugins/kernel/class_call_back_manager.cpp
        ../../src/plugins/kernel/class_meta.cpp
        ../../src/plugins/kernel/class_meta_module_impl.cpp
        ../../src/plugins/kernel/config_module_impl.cpp
        ../../src/plugins/kernel/container_impl.cpp
        ../../src/plugins/kernel/container_manager_impl.cpp
        ../../src/plugins/kernel/entity_impl.cpp
        ../../src/plugins/kernel/kernel_module_impl.cpp
        ../../src/plugins/kernel/map_module_impl.cpp
        ../../src/plugins/kernel/node_manager.cpp
        ../../src/plugins/kernel/row.cpp
        ../../src/plugins/kernel/static_entity_impl.cpp
        ../../src/plugins/kernel/table_impl.cpp
        ../../src/plugins/kernel/table_meta.cpp)
    target_link_libraries(ark_kernel_initialization_tests PRIVATE ark::kernel ark::utility ark::proto)
    add_custom_target(check-kernel-initialization
        COMMAND $<TARGET_FILE:ark_kernel_initialization_tests>
        DEPENDS ark_kernel_initialization_tests
        USES_TERMINAL)
    add_dependencies(check-core check-kernel-initialization)
endif()

if(ARK_BUILD_PROTO AND ARK_BUILD_CONFIG)
    add_subdirectory(../consul "${CMAKE_CURRENT_BINARY_DIR}/consul")
endif()

# Keep JSON optional: construction checks follow the selected runtime plugin.
if(TARGET ark_etcd_client_plugin)
    add_subdirectory(../etcd_client "${CMAKE_CURRENT_BINARY_DIR}/etcd_client")
endif()

# Keep Kafka driver discovery optional: checks follow its selected runtime plugin.
if(TARGET ark_kafka_plugin)
    add_subdirectory(../kafka "${CMAKE_CURRENT_BINARY_DIR}/kafka")
endif()

if(ARK_BUILD_PROTO AND TARGET ark_kernel_plugin AND TARGET ark_utility_plugin)
    add_executable(ark_kernel_plugin_tests ../app/kernel_plugin.cpp)
    set_property(TARGET ark_kernel_plugin_tests PROPERTY LINK_LIBRARIES ark::kernel)
    add_custom_target(check-kernel-plugin
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_kernel_plugin_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_kernel_plugin>"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/kernel plugin runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/kernel_plugin.cmake"
        DEPENDS ark_kernel_plugin_tests ark_kernel_plugin ark_utility_plugin
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-kernel-plugin)
endif()

if(ARK_BUILD_CONFIG AND TARGET timers_plugin AND TARGET module_lifecycle_plugin AND TARGET entity_operations_plugin AND
        TARGET ark_utility_plugin AND TARGET ark_kernel_plugin)
    add_executable(ark_typed_examples_tests ../app/typed_examples.cpp)
    set_property(TARGET ark_typed_examples_tests PROPERTY LINK_LIBRARIES ark::core ark::config ark::utility ark::kernel)
    add_custom_target(check-typed-examples
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_typed_examples_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:timers_plugin>" "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/typed examples runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/typed_examples.cmake"
        DEPENDS ark_typed_examples_tests timers_plugin module_lifecycle_plugin entity_operations_plugin ark_utility_plugin ark_kernel_plugin
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-typed-examples)
endif()

if(ARK_BUILD_PROTO AND TARGET ark_bus_plugin AND TARGET ark_net_plugin AND TARGET ark_utility_plugin)
    add_executable(ark_net_plugin_tests ../app/net_plugin.cpp)
    set_property(TARGET ark_net_plugin_tests PROPERTY LINK_LIBRARIES ark::net)
    add_custom_target(check-net-plugin
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_net_plugin_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_net_plugin>"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/net plugin runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/net_plugin.cmake"
        DEPENDS ark_net_plugin_tests ark_net_plugin ark_bus_plugin ark_utility_plugin
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-net-plugin)
endif()

if(ARK_BUILD_PROTO AND TARGET ark_bus_plugin AND TARGET ark_net_plugin AND TARGET ark_utility_plugin)
    add_executable(ark_bus_plugin_tests ../app/bus_plugin.cpp)
    set_property(TARGET ark_bus_plugin_tests PROPERTY LINK_LIBRARIES ark::bus)
    add_custom_target(check-bus-plugin
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_bus_plugin_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_bus_plugin>"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/bus plugin runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/bus_plugin.cmake"
        DEPENDS ark_bus_plugin_tests ark_bus_plugin ark_net_plugin ark_utility_plugin
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-bus-plugin)
endif()

if(TARGET ark_http_plugin)
    add_executable(ark_http_plugin_tests ../app/http_plugin.cpp)
    set_property(TARGET ark_http_plugin_tests PROPERTY LINK_LIBRARIES ark::http)
    add_custom_target(check-http-plugin
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_http_plugin_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:ark_http_plugin>"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/http plugin runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/http_plugin.cmake"
        DEPENDS ark_http_plugin_tests ark_http_plugin
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-http-plugin)
endif()

if(TARGET check-server-plugins)
    add_dependencies(check-core check-server-plugins)
endif()

# Legacy HTTP demonstrations remain excluded from normal applications/examples.
# Core validation uses the real sources/DSOs when their public prerequisites exist.
set(ark_http_examples_default OFF)
if(NOT ASIO_STANDALONE AND TARGET ark::http AND TARGET ark::net AND TARGET ark::etcd_client)
    set(ark_http_examples_default ON)
endif()
option(ARK_BUILD_HTTP_EXAMPLE_TESTS "Build HTTP example construction regressions" ${ark_http_examples_default})
if(ARK_BUILD_HTTP_EXAMPLE_TESTS)
    if(ASIO_STANDALONE OR NOT TARGET ark::http OR NOT TARGET ark::net OR NOT TARGET ark::etcd_client)
        message(FATAL_ERROR "HTTP example tests require Boost.Asio/Beast and ark::http, ark::net, ark::etcd_client")
    endif()
    add_subdirectory("${PROJECT_SOURCE_DIR}/examples/http_client" "${CMAKE_CURRENT_BINARY_DIR}/http_client_example")
    add_subdirectory("${PROJECT_SOURCE_DIR}/examples/http_server" "${CMAKE_CURRENT_BINARY_DIR}/http_server_example")
    add_executable(ark_http_examples_tests ../app/http_examples.cpp
        ../../examples/http_client/http_client_sample_module.cpp
        ../../examples/http_server/http_server_module.cpp)
    set_property(TARGET ark_http_examples_tests PROPERTY LINK_LIBRARIES ark::http ark::net ark::etcd_client)
    add_custom_target(check-http-examples
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:ark_http_examples_tests>"
            "-DPLUGIN_DIR=$<TARGET_FILE_DIR:http_server_plugin>"
            "-DRUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/http examples runtime"
            -P "${PROJECT_SOURCE_DIR}/tests/app/http_examples.cmake"
        DEPENDS ark_http_examples_tests http_client_sample_plugin http_server_plugin USES_TERMINAL)
    add_dependencies(check-core check-http-examples)
endif()
