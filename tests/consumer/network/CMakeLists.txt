cmake_minimum_required(VERSION 3.21)
project(ark_network_installed_consumer LANGUAGES CXX)

# Explicit installed-runtime entry; never embeds the ark source/build tree.
set(ARK_TEST_MODE network)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../install/consumer" installed)
add_custom_target(check-network-installed DEPENDS check-consumer)
