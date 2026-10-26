cmake_minimum_required(VERSION 3.21)
project(ark_murmur_hash_header_check LANGUAGES CXX)

# Only this logical public header is available; no project or third-party SDK.
set(hash_include_dir "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../../src/core/hash/murmur_hash.hpp"
    "${hash_include_dir}/ark/hash/murmur_hash.hpp" COPYONLY)
add_library(ark_murmur_hash_standalone_check OBJECT ../headers/murmur_hash.cpp)
# Drop the parent project's directory-wide ark::ark link and include paths.
set_target_properties(ark_murmur_hash_standalone_check PROPERTIES
    LINK_LIBRARIES ""
    INCLUDE_DIRECTORIES "${hash_include_dir}"
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
add_custom_target(check-murmur-hash-header DEPENDS ark_murmur_hash_standalone_check)
