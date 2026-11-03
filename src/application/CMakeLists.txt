find_package(args CONFIG REQUIRED)

set(project_name app)

if(UNIX AND (NOT APPLE))
    #Set rpath
    set(CMAKE_INSTALL_RPATH ".")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif(UNIX AND (NOT APPLE))

file(GLOB_RECURSE APP_SRC main.cpp)
add_executable(${project_name} ${APP_SRC})
target_link_libraries(${project_name} ark::config taywee::args)
if(MINGW)
    target_link_options(${project_name} PRIVATE -municode)
endif()

if(WIN32)
    target_link_libraries(${project_name})
else(UNIX)
    target_link_libraries(${project_name} pthread dl --coverage)
endif()

set_target_properties(${project_name} PROPERTIES OUTPUT_NAME_DEBUG "app_d")
set_target_properties(${project_name} PROPERTIES
    FOLDER "app"
	ARCHIVE_OUTPUT_DIRECTORY ${BIN_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${BIN_OUTPUT_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${BIN_OUTPUT_DIR})
