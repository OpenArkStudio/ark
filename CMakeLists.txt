cmake_minimum_required(VERSION 3.21)

# UNIX : is TRUE on all UNIX-like OS's, including Apple OS X and CygWin
# WIN32 : is TRUE on Windows. Prior to 2.8.4 this included CygWin
# APPLE : is TRUE on Apple systems. Note this does not imply the system is Mac OS X, only that APPLE is #defined in C/C++ header files.
# MINGW : is TRUE when using the MinGW compiler in Windows
# MSYS : is TRUE when using the MSYS developer environment in Windows
# CYGWIN : is TRUE on Windows when using the CygWin version of cmake

#---------------------------------------------------------------------------------------
# Set default build type to release
#---------------------------------------------------------------------------------------
if (UNIX)
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
    endif ()
endif ()

#---------------------------------------------------------------------------------------
# Set CMAKE_MACOSX_RPATH
#---------------------------------------------------------------------------------------
if (APPLE)
    set(CMAKE_MACOSX_RPATH 1)
endif ()

# CMake 3.9 and newer remove any effect the following settings may have on the install_name of a target on macOS:
# 
# BUILD_WITH_INSTALL_RPATH target property
# SKIP_BUILD_RPATH target property
# CMAKE_SKIP_RPATH variable
# CMAKE_SKIP_INSTALL_RPATH variable
if (APPLE AND (${CMAKE_MAJOR_VERSION} GREATER_EQUAL 3 AND ${CMAKE_MINOR_VERSION} GREATER_EQUAL 9))
    #message("cmake version is ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}")
    option(MACOS_NO_RPATH "CMake 3.9 and newer remove any effect of RPATH" ON)
endif ()

#---------------------------------------------------------------------------------------
# Compiler config
#---------------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

#---------------------------------------------------------------------------------------
# 3rd cmake modules
#---------------------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

#---------------------------------------------------------------------------------------
# CMake switches
#---------------------------------------------------------------------------------------
option(ASIO_STANDALONE "Use Standalone Asio instead of Boost.Asio" OFF)
option(ARK_BUILD_APPLICATIONS "Build ark applications and plugins" ON)
option(ARK_BUILD_PROTO "Generate and build protocol messages" ${ARK_BUILD_APPLICATIONS})
option(ARK_BUILD_NETWORK_TESTS "Build network migration regressions" OFF)
if(ASIO_STANDALONE AND (ARK_BUILD_APPLICATIONS OR ARK_BUILD_NETWORK_TESTS))
    message(FATAL_ERROR
        "ark applications and network tests require Boost.Asio and Boost.Beast for HTTP/WebSocket. "
        "Set ASIO_STANDALONE=OFF. Standalone Asio remains available for core-only and native database development builds.")
endif()
if(ARK_BUILD_APPLICATIONS AND NOT ARK_BUILD_PROTO)
    message(FATAL_ERROR "ARK_BUILD_APPLICATIONS requires ARK_BUILD_PROTO")
endif()
option(ARK_BUILD_DATABASE_TESTS "Build private native database integration development tests" OFF)
option(ARK_BUILD_BENCHMARKS "Build standalone benchmark programs" OFF)
option(ARK_BUILD_DATABASE_BENCHMARKS "Build private native database callback/coroutine benchmark" OFF)
set(ARKNET_SOURCE_DIR "" CACHE PATH "Use a local arknet checkout for development")
set(ARKNET_REVISION "a0dfe09cd543e75cfb5996e537ce2fa49f2a758f" CACHE STRING "Pinned arknet revision")
if((ARK_BUILD_DATABASE_TESTS OR ARK_BUILD_DATABASE_BENCHMARKS) AND NOT ARKNET_SOURCE_DIR)
    message(FATAL_ERROR "Database development targets require an explicit ARKNET_SOURCE_DIR with native database clients")
endif()

option(ARK_BUILD_TESTS "Enable build tests" ON)
option(ARK_BUILD_CORE_TESTS "Build core C++20 regressions" ${ARK_BUILD_TESTS})
option(ARK_BUILD_CONFIG_TOOL "Build the offline Entity JSON/CSV/XLSX exporter" OFF)
option(ARK_BUILD_CONFIG_TOOL_TESTS "Build native configuration exporter regressions" ${ARK_BUILD_TESTS})
option(ARK_BUILD_COMPRESSION_TESTS "Build bounded compression regressions (zlib, Zstandard and LZ4)" OFF)
option(ARK_BUILD_EXAMPLES "Enable build examples" ON)
set(ark_config_default OFF)
if(ARK_BUILD_APPLICATIONS OR ARK_BUILD_CORE_TESTS)
    set(ark_config_default ON)
endif()
option(ARK_BUILD_CONFIG "Build the shared application and plugin configuration reader" ${ark_config_default})
if(ARK_BUILD_APPLICATIONS AND NOT ARK_BUILD_CONFIG)
    message(FATAL_ERROR "ARK_BUILD_APPLICATIONS requires ARK_BUILD_CONFIG")
endif()

#---------------------------------------------------------------------------------------
# project switches
#---------------------------------------------------------------------------------------
option(ARK_USE_GUID_32 "use 32 bits GUID" OFF)
if (ARK_USE_GUID_32)
    add_definitions(-DARK_USE_GUID_32)
endif (ARK_USE_GUID_32)

option(ARK_DOCKER_ALPINE "build docker image by alpine" OFF)
if (ARK_DOCKER_ALPINE)
    add_definitions(-DARK_DOCKER_ALPINE)
endif (ARK_DOCKER_ALPINE)

option(ARK_OPEN_FILE_LOG "write log into file" ON)
if (ARK_OPEN_FILE_LOG)
    add_definitions(-DOPEN_FILE_LOG)
endif (ARK_OPEN_FILE_LOG)

#---------------------------------------------------------------------------------------
# Get version number.
#---------------------------------------------------------------------------------------
include(cmake/utils.cmake)
ark_extract_version()

#---------------------------------------------------------------------------------------
# ARK project
#---------------------------------------------------------------------------------------
project(ark VERSION ${ARK_VERSION} LANGUAGES CXX C)
message(STATUS "Build ark: v${ARK_VERSION}")
include(GNUInstallDirs)
option(ARK_INSTALL "Install ark public headers, selected binaries and CMake package" ${PROJECT_IS_TOP_LEVEL})
option(ARK_USE_INSTALLED_ARKNET "Resolve arknet with find_package instead of building its source" OFF)

set(ROOT_DIR ${PROJECT_SOURCE_DIR})
set(BIN_OUTPUT_DIR "${PROJECT_BINARY_DIR}/bin")

if (WIN32)
    if (MSVC_VERSION LESS 1930)
        message(FATAL_ERROR "C++20 requires Visual Studio 2022 or newer")
    endif ()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj /utf-8 /Zc:__cplusplus -D_CRT_SECURE_NO_WARNINGS")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${BIN_OUTPUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${BIN_OUTPUT_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${BIN_OUTPUT_DIR}")

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BIN_OUTPUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${BIN_OUTPUT_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${BIN_OUTPUT_DIR}")
	
	add_definitions(-DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601)
else (UNIX)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12.0)
        message(FATAL_ERROR "C++20 requires GCC 12 or newer")
    endif ()

    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -ggdb -Wall -Wextra -D_DEBUG -fPIC")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -Wextra -DNDEBUG -fPIC")
    #add_definitions(-Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -Wno-strict-aliasing -Wno-pmf-conversions -Wignored-qualifiers)
    add_definitions(-Wno-unused-parameter -Wno-unused-variable -Wno-unused-function)

    if (XCODE)
        # set the output dir for xcode
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${BIN_OUTPUT_DIR}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${BIN_OUTPUT_DIR}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${BIN_OUTPUT_DIR}")

        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BIN_OUTPUT_DIR}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${BIN_OUTPUT_DIR}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${BIN_OUTPUT_DIR}")
    endif (XCODE)

endif ()

# A tool-only build does not configure the framework's networking or plugins.
if(ARK_BUILD_CONFIG_TOOL AND NOT ARK_BUILD_APPLICATIONS AND NOT ARK_BUILD_PROTO
    AND NOT ARK_BUILD_CORE_TESTS AND NOT ARK_BUILD_NETWORK_TESTS AND NOT ARK_BUILD_CONFIG
    AND NOT ARK_BUILD_DATABASE_TESTS AND NOT ARK_BUILD_DATABASE_BENCHMARKS
    AND NOT ARK_BUILD_BENCHMARKS AND NOT ARK_BUILD_COMPRESSION_TESTS)
    set(ark_tool_install "${ARK_INSTALL}")
    set(ARK_INSTALL OFF)
    include(src/plugins/kernel/entity_json.cmake)
    set(ARK_INSTALL "${ark_tool_install}")
    add_subdirectory(tools/entity_config)
    return()
endif()

# protobuf
if(ARK_BUILD_PROTO)
    set(protobuf_MODULE_COMPATIBLE ON)
    find_package(Protobuf CONFIG QUIET)
    if(NOT Protobuf_FOUND)
        find_package(Protobuf 3.0.0 REQUIRED)
    endif()
    if (Protobuf_FOUND)
        message(STATUS "protobuf library found")
        if(TARGET protobuf::libprotobuf)
            set(PROTOBUF_LIBRARIES protobuf::libprotobuf)
        else()
            set(PROTOBUF_LIBRARIES ${Protobuf_LIBRARIES})
        endif()
        if(TARGET protobuf::protoc)
            set(PROTOBUF_PROTOC_EXECUTABLE $<TARGET_FILE:protobuf::protoc>)
        else()
            set(PROTOBUF_PROTOC_EXECUTABLE ${Protobuf_PROTOC_EXECUTABLE})
        endif()
    else ()
        message(FATAL_ERROR "protobuf library is needed but can't be found")
    endif()
endif()

if(ASIO_STANDALONE)
    set(ARKNET_USE_BOOST_ASIO OFF CACHE BOOL "Use Boost.Asio" FORCE)
else()
    set(ARKNET_USE_BOOST_ASIO ON CACHE BOOL "Use Boost.Asio" FORCE)
endif()
set(ARKNET_BUILD_TESTS OFF CACHE BOOL "Build arknet tests" FORCE)
set(ARKNET_BUILD_EXAMPLES OFF CACHE BOOL "Build arknet examples" FORCE)
set(ARKNET_BUILD_BENCHMARKS OFF CACHE BOOL "Build arknet benchmarks" FORCE)
if(ARK_USE_INSTALLED_ARKNET)
    find_package(arknet 0.2.3 CONFIG REQUIRED)
elseif(ARKNET_SOURCE_DIR)
    # arknet owns its installation/package; do not bundle a development checkout.
    add_subdirectory("${ARKNET_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/arknet" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(arknet
        GIT_REPOSITORY https://github.com/OpenArkStudio/arknet.git
        GIT_TAG ${ARKNET_REVISION}
        GIT_SHALLOW FALSE)
    FetchContent_GetProperties(arknet)
    if(NOT arknet_POPULATED)
        FetchContent_Populate(arknet)
        add_subdirectory("${arknet_SOURCE_DIR}" "${arknet_BINARY_DIR}" EXCLUDE_FROM_ALL)
    endif()
endif()

find_package(RapidXML REQUIRED)
set(RAPIDXML_INCLUDE_DIR "${RapidXML_INCLUDE_DIR}")
add_subdirectory(src/config)

add_subdirectory(src/core)

add_library(ark_sdk INTERFACE)
add_library(ark::ark ALIAS ark_sdk)
target_link_libraries(ark_sdk INTERFACE ark::core)


# include and link directories
include_directories(
        ${RAPIDXML_INCLUDE_DIR}
        ${ROOT_DIR}/src
        ${ROOT_DIR}/src/server)

link_directories(${BIN_OUTPUT_DIR})

#---------------------------------------------------------------------------------------
# log
#---------------------------------------------------------------------------------------
message(STATUS "Current system is [${CMAKE_SYSTEM}]")
message(STATUS "ROOT_DIR=${ROOT_DIR}")
message(STATUS "BIN_OUTPUT_DIR=${BIN_OUTPUT_DIR}")
message(STATUS "CMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}")

#---------------------------------------------------------------------------------------
# codecov
#---------------------------------------------------------------------------------------
#find_package(codecov)

#---------------------------------------------------------------------------------------
# sub projects
#---------------------------------------------------------------------------------------
message(STATUS "Start to build all ...")

#---------------------------------------------------------------------------------------
# Self defined postfix
#---------------------------------------------------------------------------------------
set(CMAKE_DEBUG_POSTFIX "_d" CACHE STRING "Set debug library postfix")

#---------------------------------------------------------------------------------------
# Building ARK source.
#---------------------------------------------------------------------------------------
message(STATUS "Start to build all source...")
if(ARK_BUILD_PROTO)
    add_subdirectory(src/proto)
    target_link_libraries(ark_sdk INTERFACE ark::proto)
endif()
add_subdirectory(src/plugins)
link_libraries(ark::ark)
if(ARK_BUILD_APPLICATIONS)
    add_subdirectory(src/application)
    add_subdirectory(src/server)
endif()

#---------------------------------------------------------------------------------------
# Building samples
#---------------------------------------------------------------------------------------
if(ARK_BUILD_EXAMPLES)
    add_subdirectory(examples/task_group)
endif()
if (ARK_BUILD_APPLICATIONS AND ARK_BUILD_EXAMPLES)
    message(STATUS "Building examples ...")
    add_subdirectory(examples)
endif ()

#---------------------------------------------------------------------------------------
# Building tests
#---------------------------------------------------------------------------------------
if(ARK_BUILD_TESTS)
    add_library(ark_public_header_checks OBJECT tests/headers/queue.cpp)
    target_link_libraries(ark_public_header_checks PRIVATE ark::core)
endif()

if (ARK_BUILD_APPLICATIONS AND ARK_BUILD_TESTS)
    message(STATUS "Building tests ...")
    enable_testing()
    add_subdirectory(tests)
endif ()

if(ARK_BUILD_NETWORK_TESTS)
    add_subdirectory(tests/network)
endif()

if(ARK_BUILD_CORE_TESTS)
    add_subdirectory(tests/core)
    if(TARGET check-network)
        add_dependencies(check-network check-core)
    endif()
endif()

if(ARK_BUILD_COMPRESSION_TESTS)
    add_subdirectory(tests/compression)
    if(TARGET check-core)
        add_dependencies(check-core check-compression)
    endif()
endif()

if(ARK_BUILD_BENCHMARKS OR ARK_BUILD_DATABASE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(ARK_BUILD_DATABASE_TESTS)
    add_subdirectory(tests/database)
endif()

if(ARK_BUILD_CONFIG_TOOL)
    add_subdirectory(tools/entity_config)
endif()

if(ARK_INSTALL)
    include(cmake/ark_install.cmake)
endif()
