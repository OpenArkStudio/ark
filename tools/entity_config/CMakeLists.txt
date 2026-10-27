# Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
find_package(OpenXLSX 0.5.1 CONFIG REQUIRED)
if(NOT TARGET ark::kernel_json)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../src/plugins/kernel/entity_json.cmake")
endif()
add_library(ark_entity_export STATIC tables.cpp model.cpp output.cpp)
# Full framework builds use a directory-level ark::ark link. This offline tool
# intentionally depends only on the value model and the spreadsheet reader.
set_property(TARGET ark_entity_export PROPERTY LINK_LIBRARIES "")
target_compile_features(ark_entity_export PUBLIC cxx_std_20)
target_include_directories(ark_entity_export PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ark_entity_export PUBLIC ark::kernel_json PRIVATE OpenXLSX::OpenXLSX)
add_executable(ark_config_tool main.cpp)
set_property(TARGET ark_config_tool PROPERTY LINK_LIBRARIES ark_entity_export)
if(WIN32)
    find_package(Boost CONFIG REQUIRED COMPONENTS nowide)
    target_link_libraries(ark_config_tool PRIVATE Boost::nowide)
endif()
set_target_properties(ark_config_tool PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_property(TARGET ark_config_tool PROPERTY "RUNTIME_OUTPUT_DIRECTORY_${config}" "${CMAKE_BINARY_DIR}/bin")
endforeach()
if(ARK_BUILD_CONFIG_TOOL_TESTS)
    add_executable(ark_config_tool_tests ../../tests/core/main.cpp ../../tests/config_tool/exporter.cpp)
    set_property(TARGET ark_config_tool_tests PROPERTY LINK_LIBRARIES ark_entity_export OpenXLSX::OpenXLSX)
    target_compile_definitions(ark_config_tool_tests PRIVATE
        ARK_CONFIG_TOOL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../resources/entity_config")
    set(bundle "${CMAKE_CURRENT_BINARY_DIR}/binding export")
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated include")
    add_custom_command(OUTPUT "${generated}/entity_fields.hpp"
        COMMAND $<TARGET_FILE:ark_config_tool> --schema "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/entity_config/fields.csv"
            --data "GuestPlayer=${CMAKE_CURRENT_SOURCE_DIR}/../../resources/entity_config/GuestPlayer.csv"
            --output "${bundle}" --replace
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${bundle}/code/entity_fields.hpp" "${generated}/entity_fields.hpp"
        DEPENDS ark_config_tool ../../resources/entity_config/fields.csv ../../resources/entity_config/GuestPlayer.csv
        VERBATIM)
    add_library(ark_config_generated_header_check OBJECT ../../tests/config_tool/generated_header.cpp
        "${generated}/entity_fields.hpp")
    set_property(TARGET ark_config_generated_header_check PROPERTY LINK_LIBRARIES "")
    target_compile_features(ark_config_generated_header_check PRIVATE cxx_std_20)
    target_include_directories(ark_config_generated_header_check PRIVATE "${generated}")
    add_custom_target(check-config-tool
        COMMAND $<TARGET_FILE:ark_config_tool_tests>
        COMMAND "${CMAKE_COMMAND}" "-DTOOL=$<TARGET_FILE:ark_config_tool>"
            "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/../../resources/entity_config"
            "-DRUN_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli acceptance"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/config_tool/cli.cmake"
        DEPENDS ark_config_tool_tests ark_config_tool ark_config_generated_header_check USES_TERMINAL VERBATIM)
    if(TARGET check-core)
        add_dependencies(check-core check-config-tool)
    endif()
endif()
if(ARK_INSTALL)
    install(TARGETS ark_config_tool RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/ark/config-tool/licenses" RENAME ark-LICENSE)
    install(FILES
        "${PROJECT_SOURCE_DIR}/docs/licenses/config-tool/OpenXLSX-LICENSE.md"
        "${PROJECT_SOURCE_DIR}/docs/licenses/config-tool/miniz-LICENSE.txt"
        "${PROJECT_SOURCE_DIR}/docs/licenses/config-tool/pugixml-LICENSE.md"
        "${PROJECT_SOURCE_DIR}/docs/licenses/config-tool/nlohmann-json-LICENSE.txt"
        "${PROJECT_SOURCE_DIR}/docs/licenses/config-tool/Boost-LICENSE.txt"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/ark/config-tool/licenses")
endif()
