# Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
set(consul_source "${PROJECT_SOURCE_DIR}/src/plugins/consul")

# Compile real assembly/factory locally too: private C++ symbols are not DLL exports.
add_executable(ark_consul_construction_tests ../core/main.cpp construction.cpp
    "${consul_source}/consul_plugin.cpp" "${consul_source}/consul_module_impl.cpp")
set_property(TARGET ark_consul_construction_tests PROPERTY LINK_LIBRARIES
    ark::consul ark::proto ark::config tomlplusplus::tomlplusplus)
target_include_directories(ark_consul_construction_tests PRIVATE "${consul_source}")

# A controlled module exercises the actual entry/exit functions without HTTP or Consul services.
add_library(ark_consul_entry_probe SHARED entry_factory.cpp "${consul_source}/consul_plugin.cpp")
set_property(TARGET ark_consul_entry_probe PROPERTY LINK_LIBRARIES ark::consul ark::proto)
target_include_directories(ark_consul_entry_probe PRIVATE "${consul_source}")
set_target_properties(ark_consul_entry_probe PROPERTIES PREFIX "")
add_executable(ark_consul_entry_tests entry.cpp)
set_property(TARGET ark_consul_entry_tests PROPERTY LINK_LIBRARIES ark::consul ark::proto)

add_custom_target(check-consul-construction
    COMMAND $<TARGET_FILE:ark_consul_construction_tests>
    COMMAND $<TARGET_FILE:ark_consul_entry_tests> "$<TARGET_FILE_DIR:ark_consul_entry_probe>/" "ark_consul_entry_probe"
    DEPENDS ark_consul_construction_tests ark_consul_entry_tests ark_consul_entry_probe
    USES_TERMINAL VERBATIM)

if(TARGET ark_consul_plugin)
    # Keep real DSO export inspection separate from the host linking static AFProto.
    add_executable(ark_consul_exports_tests exports.cpp)
    set_property(TARGET ark_consul_exports_tests PROPERTY LINK_LIBRARIES ark::core)
    add_custom_target(check-consul-plugin
        COMMAND $<TARGET_FILE:ark_consul_exports_tests> "$<TARGET_FILE_DIR:ark_consul_plugin>/" "ark_consul_plugin"
        DEPENDS ark_consul_exports_tests ark_consul_plugin check-consul-construction
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-consul-plugin)
else()
    add_dependencies(check-core check-consul-construction)
endif()
