# Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
if(TARGET ark_kernel_json)
    return()
endif()
include(GNUInstallDirs)
find_package(nlohmann_json CONFIG REQUIRED)
set(ark_entity_json_directory "${CMAKE_CURRENT_LIST_DIR}")
set(ark_entity_json_include "${CMAKE_CURRENT_BINARY_DIR}/entity-json-include")
file(MAKE_DIRECTORY "${ark_entity_json_include}/ark/kernel")
configure_file("${ark_entity_json_directory}/entity_json.hpp"
    "${ark_entity_json_include}/ark/kernel/entity_json.hpp" COPYONLY)
add_library(ark_kernel_json STATIC "${ark_entity_json_directory}/entity_json.cpp")
add_library(ark::kernel_json ALIAS ark_kernel_json)
set_target_properties(ark_kernel_json PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME kernel_json)
target_compile_features(ark_kernel_json PUBLIC cxx_std_20)
target_include_directories(ark_kernel_json PUBLIC
    "$<BUILD_INTERFACE:${ark_entity_json_include}>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
# JSON is entirely compiled into this std-only value-model library.
target_link_libraries(ark_kernel_json PRIVATE "$<BUILD_INTERFACE:nlohmann_json::nlohmann_json>")
if(ARK_INSTALL)
    install(TARGETS ark_kernel_json EXPORT arkKernelJsonTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(EXPORT arkKernelJsonTargets NAMESPACE ark:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ark")
    if(NOT TARGET ark_kernel_api)
        install(FILES "${ark_entity_json_directory}/entity_json.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ark/kernel")
    endif()
endif()
if(ARK_BUILD_CORE_TESTS)
    add_executable(ark_entity_json_tests "${ark_entity_json_directory}/../../../tests/core/main.cpp"
        "${ark_entity_json_directory}/../../../tests/kernel_json/model.cpp")
    set_property(TARGET ark_entity_json_tests PROPERTY LINK_LIBRARIES ark::kernel_json)
    add_custom_target(check-entity-json COMMAND $<TARGET_FILE:ark_entity_json_tests>
        DEPENDS ark_entity_json_tests USES_TERMINAL VERBATIM)
endif()
