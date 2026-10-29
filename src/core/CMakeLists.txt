find_package(spdlog 1.17 CONFIG REQUIRED)
find_package(readerwriterqueue CONFIG REQUIRED)

# Source headers live beside core code; preserve logical <ark/...> includes.
set(ark_core_public_headers
    "app.hpp"
    "array_pod.hpp"
    "buffer.hpp"
    "bus.hpp"
    "client_connect.hpp"
    "compress/lz4.hpp"
    "compress/zlib.hpp"
    "compress/zstd.hpp"
    "container/array_map.hpp"
    "container/bit_value.hpp"
    "container/consistent_hash.hpp"
    "container/list.hpp"
    "container/lock_free_queue.hpp"
    "container/lru_map.hpp"
    "container/map.hpp"
    "container/queue.hpp"
    "container/thread_safe_queue.hpp"
    "container/vector.hpp"
    "core_def.hpp"
    "cron.hpp"
    "crypto/xxtea.hpp"
    "define.hpp"
    "dep.hpp"
    "dynamic_library.hpp"
    "enum.hpp"
    "error.hpp"
    "error_code.hpp"
    "event_define.hpp"
    "guid128.hpp"
    "hash/base64.hpp"
    "hash/crc.hpp"
    "hash/murmur_hash.hpp"
    "lexical_cast.hpp"
    "lock/rw_lock.hpp"
    "lock/spin_lock.hpp"
    "logger.hpp"
    "macros.hpp"
    "math/vector3d.hpp"
    "meta_define.hpp"
    "misc.hpp"
    "module.hpp"
    "module_base.hpp"
    "noncopyable.hpp"
    "pattern/singleton.hpp"
    "platform.hpp"
    "plugin.hpp"
    "plugin_base.hpp"
    "plugin_manager.hpp"
    "proto.hpp"
    "random/random_generator.hpp"
    "random/random_xoshiro.hpp"
    "socket_func.hpp"
    "spdlog/sinks/date_and_hour_file_sink.h"
    "spdlog/sinks/rotating_file_with_date_sink-inl.h"
    "spdlog/sinks/rotating_file_with_date_sink.h"
    "string.hpp"
    "string_pod.hpp"
    "string_utils.hpp"
    "task.hpp"
    "startup_cleanup.hpp"
    "time/date_time.hpp"
    "time/timer.hpp"
    "uid_generator.hpp"
    "xml.hpp"
)
set(ark_core_include_dir "${CMAKE_CURRENT_BINARY_DIR}/public-include")
set(mapped_headers)
foreach(header IN LISTS ark_core_public_headers)
    set(mapped_header "${ark_core_include_dir}/ark/${header}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${header}" "${mapped_header}" COPYONLY)
    list(APPEND mapped_headers "${mapped_header}")
    if(ARK_INSTALL)
        get_filename_component(header_dir "${header}" DIRECTORY)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${header}"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ark/${header_dir}")
    endif()
endforeach()
# Clean only this target's generated header directory, never source files.
file(GLOB_RECURSE previous_headers LIST_DIRECTORIES FALSE "${ark_core_include_dir}/ark/*")
foreach(header IN LISTS previous_headers)
    if(NOT header IN_LIST mapped_headers)
        file(REMOVE "${header}")
    endif()
endforeach()

add_library(ark_core INTERFACE)
add_library(ark::core ALIAS ark_core)
target_compile_features(ark_core INTERFACE cxx_std_20)
target_include_directories(ark_core INTERFACE
    "$<BUILD_INTERFACE:${ark_core_include_dir}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(ark_core INTERFACE ark::config_headers arknet::arknet spdlog::spdlog_header_only
    readerwriterqueue::readerwriterqueue RapidXML::RapidXML ${CMAKE_DL_LIBS})
target_compile_definitions(ark_core INTERFACE
    $<$<BOOL:${ARK_USE_GUID_32}>:ARK_USE_GUID_32>
    $<$<BOOL:${ARK_DOCKER_ALPINE}>:ARK_DOCKER_ALPINE>
    $<$<BOOL:${ARK_OPEN_FILE_LOG}>:OPEN_FILE_LOG>
    $<$<CONFIG:Debug>:_DEBUG>)
if(MSVC)
    target_compile_options(ark_core INTERFACE /bigobj /utf-8 /Zc:__cplusplus /Zc:preprocessor)
    target_compile_definitions(ark_core INTERFACE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()
