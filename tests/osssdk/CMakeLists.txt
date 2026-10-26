# Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
set(oss_sdk_source "${PROJECT_SOURCE_DIR}/src/plugins/osssdk")

# Compile real assembly/factory locally too: private C++ symbols are not DLL exports.
add_executable(ark_oss_sdk_construction_tests ../core/main.cpp construction.cpp
    "${oss_sdk_source}/oss_sdk_plugin.cpp" "${oss_sdk_source}/oss_sdk_module_impl.cpp")
set_property(TARGET ark_oss_sdk_construction_tests PROPERTY LINK_LIBRARIES
    ark::osssdk)
target_include_directories(ark_oss_sdk_construction_tests PRIVATE "${oss_sdk_source}")

# A controlled module exercises the actual entry/exit functions without object-storage services.
add_library(ark_oss_sdk_entry_probe SHARED entry_factory.cpp "${oss_sdk_source}/oss_sdk_plugin.cpp")
set_property(TARGET ark_oss_sdk_entry_probe PROPERTY LINK_LIBRARIES ark::osssdk)
target_include_directories(ark_oss_sdk_entry_probe PRIVATE "${oss_sdk_source}")
set_target_properties(ark_oss_sdk_entry_probe PROPERTIES PREFIX "")
add_executable(ark_oss_sdk_entry_tests entry.cpp)
set_property(TARGET ark_oss_sdk_entry_tests PROPERTY LINK_LIBRARIES ark::osssdk)

add_custom_target(check-oss-sdk-construction
    COMMAND $<TARGET_FILE:ark_oss_sdk_construction_tests>
    COMMAND $<TARGET_FILE:ark_oss_sdk_entry_tests> "$<TARGET_FILE_DIR:ark_oss_sdk_entry_probe>/" "ark_oss_sdk_entry_probe"
    DEPENDS ark_oss_sdk_construction_tests ark_oss_sdk_entry_tests ark_oss_sdk_entry_probe
    USES_TERMINAL VERBATIM)

if(TARGET ark_oss_sdk_plugin)
    # Keep real DSO export inspection separate from the host linking static AFProto.
    add_executable(ark_oss_sdk_exports_tests exports.cpp)
    set_property(TARGET ark_oss_sdk_exports_tests PROPERTY LINK_LIBRARIES ark::core)
    add_custom_target(check-oss-sdk-plugin
        COMMAND $<TARGET_FILE:ark_oss_sdk_exports_tests> "$<TARGET_FILE_DIR:ark_oss_sdk_plugin>/" "ark_oss_sdk_plugin"
        DEPENDS ark_oss_sdk_exports_tests ark_oss_sdk_plugin check-oss-sdk-construction
        USES_TERMINAL VERBATIM)
    add_dependencies(check-core check-oss-sdk-plugin)
else()
    add_dependencies(check-core check-oss-sdk-construction)
endif()
