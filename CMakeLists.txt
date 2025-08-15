cmake_minimum_required(VERSION 3.10)
project(socket_io_serverpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(UUID REQUIRED uuid)

# Add include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/common
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/lib/rapidjson
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/lib/rapidjson/internal
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/scgi
    ${PROJECT_SOURCE_DIR}/external/websocketpp
)

# Collect source files
file(GLOB_RECURSE SOURCES
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/*.cpp
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/common/*.cpp
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/scgi/*.cpp
)

# Add main examples (usage.cpp, test1.cpp)
add_executable(usage examples/usage.cpp ${SOURCES})
add_executable(test1 examples/test1.cpp ${SOURCES})

# Link against UUID library
target_link_libraries(usage ${UUID_LIBRARIES})
target_link_libraries(test1 ${UUID_LIBRARIES})

# Optionally, you can add targets for static/shared library
# add_library(socket_io_serverpp STATIC ${SOURCES})
# add_library(socket_io_serverpp SHARED ${SOURCES})
