cmake_minimum_required(VERSION 3.10.0)
project(Server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(Server INTERFACE)
add_library(Server::Server ALIAS Server)

target_sources(
    Server
    INTERFACE
    FILE_SET HEADERS TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/include
    FILES ${CMAKE_CURRENT_LIST_DIR}/include/server/server.h
)

find_package(boost_beast CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

target_link_libraries(Server INTERFACE Boost::beast OpenSSL::SSL OpenSSL::Crypto)

install(TARGETS Server
    EXPORT ServerTargets
    FILE_SET HEADERS)

install(EXPORT ServerTargets
    DESTINATION share/cmake/server
    NAMESPACE Server::)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_LIST_DIR}/serverConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ServerConfig.cmake
    INSTALL_DESTINATION share/cmake/server
    PATH_VARS CMAKE_INSTALL_PREFIX)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ServerConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ServerConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ServerConfigVersion.cmake
    DESTINATION share/cmake/server
)