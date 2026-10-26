# Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
find_package(nlohmann_json CONFIG REQUIRED)
set(etcd_client_source "${PROJECT_SOURCE_DIR}/src/plugins/etcd_client")

# Compile real assembly/factory locally too: private C++ symbols are not DLL exports.
add_executable(ark_etcd_client_construction_tests ../core/main.cpp construction.cpp
    "${etcd_client_source}/etcd_client_plugin.cpp" "${etcd_client_source}/etcd_client_module_impl.cpp" "${etcd_client_source}/etcd_client_impl.cpp")
set_property(TARGET ark_etcd_client_construction_tests PROPERTY LINK_LIBRARIES
    ark::etcd_client nlohmann_json::nlohmann_json)
target_include_directories(ark_etcd_client_construction_tests PRIVATE "${etcd_client_source}")

# A controlled module exercises the actual entry/exit functions without HTTP or Etcd Client services.
add_library(ark_etcd_client_entry_probe SHARED entry_factory.cpp "${etcd_client_source}/etcd_client_plugin.cpp")
set_property(TARGET ark_etcd_client_entry_probe PROPERTY LINK_LIBRARIES ark::etcd_client)
target_include_directories(ark_etcd_client_entry_probe PRIVATE "${etcd_client_source}")
set_target_properties(ark_etcd_client_entry_probe PROPERTIES PREFIX "")
add_executable(ark_etcd_client_entry_tests entry.cpp)
set_property(TARGET ark_etcd_client_entry_tests PROPERTY LINK_LIBRARIES ark::etcd_client)

add_custom_target(check-etcd-client-construction
    COMMAND $<TARGET_FILE:ark_etcd_client_construction_tests>
    COMMAND $<TARGET_FILE:ark_etcd_client_entry_tests> "$<TARGET_FILE_DIR:ark_etcd_client_entry_probe>/" "ark_etcd_client_entry_probe"
    DEPENDS ark_etcd_client_construction_tests ark_etcd_client_entry_tests ark_etcd_client_entry_probe
    USES_TERMINAL VERBATIM)

if(TARGET ark_etcd_client_plugin)
    # Keep real DSO export inspection separate from the host linking static AFProto.
    add_executable(ark_etcd_client_exports_tests exports.cpp)
    set_property(TARGET ark_etcd_client_exports_tests PROPERTY LINK_LIBRARIES ark::core)
    add_custom_target(check-etcd-client-plugin
        COMMAND $<TARGET_FILE:ark_etcd_client_exports_tests> "$<TARGET_FILE_DIR:ark_etcd_client_plugin>/" "ark_etcd_client_plugin"
        DEPENDS ark_etcd_client_exports_tests ark_etcd_client_plugin check-etcd-client-construction
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-etcd-client-plugin check-etcd-client-construction)
endif()
