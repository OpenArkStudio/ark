# Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
cmake_minimum_required(VERSION 3.21)
foreach(argument SOURCE_DIR RUN_DIR)
    if(NOT DEFINED ${argument})
        message(FATAL_ERROR "${argument} is required")
    endif()
endforeach()
if(NOT DEFINED GENERATOR)
    set(GENERATOR Ninja)
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef token)
set(fixture "${RUN_DIR}/${token}")
file(MAKE_DIRECTORY "${fixture}/external/OpenXLSX" "${fixture}/external/pugixml"
    "${fixture}/external/miniz" "${fixture}/external/Boost" "${fixture}/bundled/OpenXLSX"
    "${fixture}/bundled/Boost" "${fixture}/baseline" "${fixture}/consumer")

# Reproduce the package contract: OpenXLSX exposes dependency targets but does
# not discover them. No third-party headers or binary libraries are needed to
# exercise CMake generation and the production tool's dependency discovery.
file(WRITE "${fixture}/external/OpenXLSX/OpenXLSXConfig.cmake" [=[
add_library(OpenXLSX::OpenXLSX INTERFACE IMPORTED GLOBAL)
set_property(TARGET OpenXLSX::OpenXLSX PROPERTY INTERFACE_LINK_LIBRARIES
    "$<LINK_ONLY:pugixml::static>;$<LINK_ONLY:miniz::miniz>")
]=])
file(WRITE "${fixture}/bundled/OpenXLSX/OpenXLSXConfig.cmake" [=[
add_library(OpenXLSX::OpenXLSX INTERFACE IMPORTED GLOBAL)
]=])
include(CMakePackageConfigHelpers)
foreach(shape external bundled)
    write_basic_package_version_file("${fixture}/${shape}/OpenXLSX/OpenXLSXConfigVersion.cmake"
        VERSION 0.5.1 COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
    # Windows tool configuration also requires Nowide. Keep this fixture focused
    # on discovery rather than depending on a particular local Boost installation.
    file(WRITE "${fixture}/${shape}/Boost/BoostConfig.cmake" [=[
add_library(Boost::nowide INTERFACE IMPORTED GLOBAL)
set(Boost_FOUND TRUE)
set(Boost_nowide_FOUND TRUE)
]=])
endforeach()
file(WRITE "${fixture}/external/pugixml/pugixmlConfig.cmake"
    "add_library(pugixml::static INTERFACE IMPORTED GLOBAL)\n")
file(WRITE "${fixture}/external/miniz/minizConfig.cmake"
    "add_library(miniz::miniz INTERFACE IMPORTED GLOBAL)\n")
file(WRITE "${fixture}/baseline/main.cpp" "int main() {}\n")
file(WRITE "${fixture}/baseline/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.21)
project(missing_dependency_reproduction LANGUAGES CXX)
find_package(OpenXLSX 0.5.1 CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE OpenXLSX::OpenXLSX)
]=])
set(consumer [=[
cmake_minimum_required(VERSION 3.21)
project(config_tool_dependency_regression LANGUAGES CXX)
set(ARK_BUILD_CONFIG_TOOL_TESTS OFF)
set(ARK_INSTALL OFF)
add_library(ark::kernel_json INTERFACE IMPORTED GLOBAL)
add_subdirectory("@SOURCE_DIR@/tools/entity_config" config-tool)
if(NOT TARGET ark_config_tool)
    message(FATAL_ERROR "Production tool target was not configured")
endif()
]=])
string(CONFIGURE "${consumer}" consumer @ONLY)
file(WRITE "${fixture}/consumer/CMakeLists.txt" "${consumer}")

function(configure_shape name source shape expected)
    set(arguments -S "${fixture}/${source}" -B "${fixture}/${name}" -G "${GENERATOR}"
        "-DCMAKE_PREFIX_PATH=${fixture}/${shape}"
        -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF)
    if(shape STREQUAL bundled)
        list(APPEND arguments -DCMAKE_DISABLE_FIND_PACKAGE_pugixml=ON -DCMAKE_DISABLE_FIND_PACKAGE_miniz=ON)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" ${arguments}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 60)
    file(WRITE "${fixture}/${name}.log" "${output}\n${error}")
    if(expected STREQUAL failure)
        if(result EQUAL 0 OR NOT "${output}\n${error}" MATCHES "pugixml::static")
            message(FATAL_ERROR "Missing-target baseline did not reproduce: ${result}\n${output}\n${error}")
        endif()
    elseif(NOT result EQUAL 0)
        message(FATAL_ERROR "${name} configuration failed: ${result}\n${output}\n${error}")
    endif()
endfunction()

configure_shape(missing-target baseline external failure)
configure_shape(external-discovery consumer external success)
configure_shape(bundled-without-external-packages consumer bundled success)
message(STATUS "OpenXLSX missing-target reproduction, external discovery and bundled-package configuration passed")
