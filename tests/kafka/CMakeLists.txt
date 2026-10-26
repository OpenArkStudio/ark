# Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
# Match production's Unix-only discovery; Windows keeps its existing external setup.
if(NOT WIN32)
    find_package(librdkafka REQUIRED)
endif()
set(kafka_source "${PROJECT_SOURCE_DIR}/src/plugins/kafka")

# Compile real assembly/factory too: private C++ symbols are not DLL exports.
add_executable(ark_kafka_construction_tests ../core/main.cpp construction.cpp
    "${kafka_source}/kafka_plugin.cpp" "${kafka_source}/kafka_module_impl.cpp")
set_property(TARGET ark_kafka_construction_tests PROPERTY LINK_LIBRARIES
    ark::kafka "${LIBRDKAFKA_CPP_LIBRARIES}" "${LIBRDKAFKA_LIBRARIES}")
target_include_directories(ark_kafka_construction_tests PRIVATE "${kafka_source}" "${LIBRDKAFKA_INCLUDE_DIR}")

# The controlled module exercises production entry/exit without a producer or broker.
add_library(ark_kafka_entry_probe SHARED entry_factory.cpp "${kafka_source}/kafka_plugin.cpp")
set_property(TARGET ark_kafka_entry_probe PROPERTY LINK_LIBRARIES ark::kafka)
target_include_directories(ark_kafka_entry_probe PRIVATE "${kafka_source}")
set_target_properties(ark_kafka_entry_probe PROPERTIES PREFIX "")
add_executable(ark_kafka_entry_tests entry.cpp)
set_property(TARGET ark_kafka_entry_tests PROPERTY LINK_LIBRARIES ark::kafka)

add_custom_target(check-kafka-construction
    COMMAND $<TARGET_FILE:ark_kafka_construction_tests>
    COMMAND $<TARGET_FILE:ark_kafka_entry_tests> "$<TARGET_FILE_DIR:ark_kafka_entry_probe>/" "ark_kafka_entry_probe"
    DEPENDS ark_kafka_construction_tests ark_kafka_entry_tests ark_kafka_entry_probe
    USES_TERMINAL VERBATIM)

add_executable(ark_kafka_exports_tests exports.cpp)
set_property(TARGET ark_kafka_exports_tests PROPERTY LINK_LIBRARIES ark::core)
add_custom_target(check-kafka-plugin
    COMMAND $<TARGET_FILE:ark_kafka_exports_tests> "$<TARGET_FILE_DIR:ark_kafka_plugin>/" "ark_kafka_plugin"
    DEPENDS ark_kafka_exports_tests ark_kafka_plugin check-kafka-construction
    USES_TERMINAL VERBATIM)
add_dependencies(check-core check-kafka-plugin check-kafka-construction)
