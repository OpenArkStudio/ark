cmake_minimum_required(VERSION 3.21)
project(ark_database_installed_consumer LANGUAGES CXX)

# Explicit installed-runtime entry; never embeds the ark source/build tree.
set(ARK_TEST_MODE database)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../install/consumer" installed)
add_custom_target(check-database-installed DEPENDS check-consumer)
